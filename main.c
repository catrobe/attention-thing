// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 M. Ömer Okyar

#include <ctype.h>     // toupper, tolower
#include <errno.h>     // errno, EAGAIN, EEXIST
#include <stdio.h>     // printf, snprintf, perror, fopen, fgets
#include <stdlib.h>    // getenv, atexit, qsort, strtol
#include <string.h>    // strlen, strcmp, strrchr, strstr, strerror
#include <time.h>      // time_t, time, localtime, mktime, strftime
#include <dirent.h>    // opendir, readdir, closedir
#include <sys/stat.h>  // stat, mkdir
#include <sys/ioctl.h> // ioctl, TIOCGWINSZ: terminal size
#include <termios.h>   // tcgetattr, tcsetattr: raw mode
#include <unistd.h>    // read, write, isatty

#define VERSION     "0.1.0"
#define MAX_ENTRIES 128
#define MAX_BODY    4096  // longest note text we keep, in bytes
#define MAX_PICKS   3     // how many notes you pick at a time
#define MAX_SEEN    1024  // how many first-seen dates we remember
#define DAILY_GOAL  3     // the "/3" in "done 2/3"

typedef struct {
	char   path[1024];     // full path to the note file
	char   name[256];      // file name, e.g. "cubesat.md"
	char   title[256];     // file name without ".md" / ".txt"
	char   bucket[16];     // "now" or "try"
	time_t mtime;          // last modified, in seconds since 1970
	time_t first_seen;     // the day atthing first saw this note
	char   body[MAX_BODY]; // text inside the note, "" if the note is empty
} Entry;

Entry entries[MAX_ENTRIES];
int n_entries = 0;

char base[512];        // the notes folder, e.g. ~/Life/forgetme
char state_dir[600];   // atthing's own memory: base/.atthing

// ---- Small helpers ------------------------------------------------------------

// Returns 1 if s ends with suffix, 0 if it doesn't.
int ends_with(const char *s, const char *suffix) {
	size_t len_s = strlen(s);
	size_t len_suffix = strlen(suffix);

	if (len_suffix > len_s) {
		return 0;
	}
	// s + (len_s - len_suffix) points at the last len_suffix characters of s
	return strcmp(s + (len_s - len_suffix), suffix) == 0;
}

// Returns 1 if a file with this name should show up as a note.
int is_note(const char *name) {
	if (name[0] == '.') {
		return 0;   // hidden files, ".", ".."
	}
	if (strstr(name, ".sync-conflict-") != NULL) {
		return 0;   // Syncthing's conflict copies
	}
	return ends_with(name, ".md") || ends_with(name, ".txt");
}

// Creates the folder if it isn't there yet. Returns 0 if it exists afterwards.
int ensure_dir(const char *path) {
	if (mkdir(path, 0755) == 0 || errno == EEXIST) {
		return 0;
	}
	return -1;
}

// ---- Dates ------------------------------------------------------------------------

// Writes the local date of t as "2026-09-10" into out.
void date_string(time_t t, char *out, size_t size) {
	struct tm *tm = localtime(&t);
	if (tm == NULL || strftime(out, size, "%Y-%m-%d", tm) == 0) {
		snprintf(out, size, "0000-00-00");
	}
}

// Turns "2026-09-10" into noon of that day. Returns -1 if it isn't a date.
time_t parse_date(const char *s) {
	int y, m, d;
	if (sscanf(s, "%4d-%2d-%2d", &y, &m, &d) != 3) {
		return (time_t)-1;
	}
	struct tm tm = {0};
	tm.tm_year = y - 1900;
	tm.tm_mon = m - 1;
	tm.tm_mday = d;
	tm.tm_hour = 12;
	tm.tm_isdst = -1;   // let the system work out summer time
	return mktime(&tm);
}

// Whole calendar days from `from` to `to`, in local time. Midnight is the
// boundary: 23:59 and 00:01 the next day are 1 day apart.
long days_between(time_t from, time_t to) {
	char a[16], b[16];
	date_string(from, a, sizeof a);
	date_string(to, b, sizeof b);
	time_t noon_a = parse_date(a);
	time_t noon_b = parse_date(b);
	if (noon_a == (time_t)-1 || noon_b == (time_t)-1) {
		return 0;
	}
	double seconds = difftime(noon_b, noon_a);
	return (long)((seconds + 43200) / 86400);   // round, in case of summer time
}

// 75 -> "01:15", 3725 -> "1:02:05"
void format_duration(long seconds, char *out, size_t size) {
	if (seconds >= 3600) {
		snprintf(out, size, "%ld:%02ld:%02ld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
	} else {
		snprintf(out, size, "%02ld:%02ld", seconds / 60, seconds % 60);
	}
}

// ---- Saving files safely ------------------------------------------------------
// We write to "file.tmp" first and then rename it over the real file. rename()
// swaps the file in one step, so a crash or a sync never sees a half-written file.

FILE *start_write(const char *path, char *tmp, size_t tmp_size) {
	snprintf(tmp, tmp_size, "%s.tmp", path);
	return fopen(tmp, "w");
}

int finish_write(FILE *f, const char *tmp, const char *path) {
	if (fclose(f) != 0) {
		return -1;
	}
	return rename(tmp, path);
}

// ---- First-seen dates (.atthing/seen) ----------------------------------------
// One line per note: "2026-09-10<TAB>cubesat.md". Keyed by file name, so a note
// keeps its date when it moves between folders.

typedef struct {
	char   name[256];
	time_t date;
} Seen;

Seen seen[MAX_SEEN];
int n_seen = 0;
int seen_changed = 0;

void load_seen(void) {
	n_seen = 0;
	char path[1024];
	snprintf(path, sizeof path, "%s/seen", state_dir);
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return;   // first run: nothing remembered yet
	}
	char line[600];
	while (n_seen < MAX_SEEN && fgets(line, sizeof line, f) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		char *tab = strchr(line, '\t');
		if (tab == NULL) {
			continue;
		}
		*tab = '\0';   // split the line into the date and the name
		time_t date = parse_date(line);
		if (date == (time_t)-1) {
			continue;
		}
		snprintf(seen[n_seen].name, sizeof seen[n_seen].name, "%s", tab + 1);
		seen[n_seen].date = date;
		n_seen++;
	}
	fclose(f);
}

void save_seen(void) {
	char path[1024], tmp[1100];
	snprintf(path, sizeof path, "%s/seen", state_dir);
	FILE *f = start_write(path, tmp, sizeof tmp);
	if (f == NULL) {
		return;
	}
	for (int i = 0; i < n_seen; i++) {
		char date[16];
		date_string(seen[i].date, date, sizeof date);
		fprintf(f, "%s\t%s\n", date, seen[i].name);
	}
	finish_write(f, tmp, path);
	seen_changed = 0;
}

// When did atthing first see this note? If never, remember `guess` as the date.
time_t first_seen(const char *name, time_t guess) {
	for (int i = 0; i < n_seen; i++) {
		if (strcmp(seen[i].name, name) == 0) {
			return seen[i].date;
		}
	}
	if (n_seen < MAX_SEEN) {
		snprintf(seen[n_seen].name, sizeof seen[n_seen].name, "%s", name);
		seen[n_seen].date = guess;
		n_seen++;
		seen_changed = 1;
	}
	return guess;
}

// ---- Today's picks (.atthing/today) ------------------------------------------
// Line 1: the date. Line 2: "done N". Then one picked note per line, as
// "bucket/name", e.g. "now/cubesat.md".

char picks[MAX_PICKS][600];
int n_picks = 0;
int done_today = 0;

void save_today(void) {
	char path[1024], tmp[1100], date[16];
	snprintf(path, sizeof path, "%s/today", state_dir);
	date_string(time(NULL), date, sizeof date);
	FILE *f = start_write(path, tmp, sizeof tmp);
	if (f == NULL) {
		return;
	}
	fprintf(f, "%s\ndone %d\n", date, done_today);
	for (int i = 0; i < n_picks; i++) {
		fprintf(f, "%s\n", picks[i]);
	}
	finish_write(f, tmp, path);
}

// Loads today's picks. If the file is from another day, starts fresh.
void load_today(void) {
	n_picks = 0;
	done_today = 0;
	char path[1024], today[16], line[600];
	snprintf(path, sizeof path, "%s/today", state_dir);
	date_string(time(NULL), today, sizeof today);

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return;
	}
	if (fgets(line, sizeof line, f) == NULL || strncmp(line, today, 10) != 0) {
		fclose(f);   // yesterday's picks (or broken file): a new day starts empty
		return;
	}
	if (fgets(line, sizeof line, f) != NULL) {
		sscanf(line, "done %d", &done_today);
	}
	while (n_picks < MAX_PICKS && fgets(line, sizeof line, f) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] != '\0') {
			snprintf(picks[n_picks], sizeof picks[n_picks], "%s", line);
			n_picks++;
		}
	}
	fclose(f);
}

// Which entry is "now/cubesat.md"? Returns its index, or -1 if it's gone.
int find_entry(const char *pick) {
	for (int i = 0; i < n_entries; i++) {
		char rel[300];
		snprintf(rel, sizeof rel, "%s/%s", entries[i].bucket, entries[i].name);
		if (strcmp(rel, pick) == 0) {
			return i;
		}
	}
	return -1;
}

// Drops picks whose note was moved or deleted outside atthing.
void prune_picks(void) {
	int kept = 0;
	for (int i = 0; i < n_picks; i++) {
		if (find_entry(picks[i]) != -1) {
			if (kept != i) {
				memcpy(picks[kept], picks[i], sizeof picks[kept]);
			}
			kept++;
		}
	}
	if (kept != n_picks) {
		n_picks = kept;
		save_today();
	}
}

// Returns 1 if entries[index] is one of today's picks.
int is_picked(int index) {
	char rel[300];
	snprintf(rel, sizeof rel, "%s/%s", entries[index].bucket, entries[index].name);
	for (int i = 0; i < n_picks; i++) {
		if (strcmp(picks[i], rel) == 0) {
			return 1;
		}
	}
	return 0;
}

// ---- Finding notes --------------------------------------------------------------

// Reads the text inside the file at path into body (at most size - 1 bytes).
// If the file is empty or can't be opened, body becomes "".
void read_body(const char *path, char *body, size_t size) {
	body[0] = '\0';

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return;
	}
	size_t n = fread(body, 1, size - 1, f);
	fclose(f);
	body[n] = '\0';

	// drop newlines and spaces at the very end
	while (n > 0 && (body[n - 1] == '\n' || body[n - 1] == '\r' ||
	                 body[n - 1] == ' '  || body[n - 1] == '\t')) {
		n--;
		body[n] = '\0';
	}
}

// Adds every note inside base/bucket to entries[].
// Returns 0 if the folder could be opened, -1 if not.
int bucket_scan(const char *bucket) {
	char dir[600];
	snprintf(dir, sizeof dir, "%s/%s", base, bucket);

	DIR *d = opendir(dir);
	if (d == NULL) {
		return -1;
	}

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (!is_note(ent->d_name)) {
			continue;   // skip ".", "..", hidden files, and anything that isn't a note
		}
		if (n_entries >= MAX_ENTRIES) {
			break;      // array is full, stop instead of writing past the end
		}

		Entry *e = &entries[n_entries];   // e points at the next free slot

		snprintf(e->path, sizeof e->path, "%s/%s", dir, ent->d_name);
		snprintf(e->name, sizeof e->name, "%s", ent->d_name);
		snprintf(e->bucket, sizeof e->bucket, "%s", bucket);
		read_body(e->path, e->body, sizeof e->body);

		snprintf(e->title, sizeof e->title, "%s", ent->d_name);
		char *dot = strrchr(e->title, '.');   // the last "." in the name
		if (dot != NULL) {
			*dot = '\0';                      // cut off ".md" or ".txt"
		}

		struct stat st;
		if (stat(e->path, &st) == 0) {
			e->mtime = st.st_mtime;
		} else {
			e->mtime = 0;
		}
		e->first_seen = e->mtime;

		n_entries++;
	}

	closedir(d);
	return 0;
}

// Sort order: "now" before "try" (alphabetical works for those two), then by
// title, ignoring upper/lower case.
int compare_entries(const void *a, const void *b) {
	const Entry *x = a;
	const Entry *y = b;
	int by_bucket = strcmp(x->bucket, y->bucket);
	if (by_bucket != 0) {
		return by_bucket;
	}
	const unsigned char *p = (const unsigned char *)x->title;
	const unsigned char *q = (const unsigned char *)y->title;
	while (*p != '\0' && tolower(*p) == tolower(*q)) {
		p++;
		q++;
	}
	return tolower(*p) - tolower(*q);
}

// Finds all notes again. Returns how many of the two folders could be opened.
int scan_notes(void) {
	n_entries = 0;
	int found = 0;
	if (bucket_scan("now") == 0) found++;
	if (bucket_scan("try") == 0) found++;
	qsort(entries, n_entries, sizeof entries[0], compare_entries);
	return found;
}

// Rescans the notes and reloads atthing's memory. Used by the full screen.
void reload(void) {
	scan_notes();
	load_seen();
	for (int i = 0; i < n_entries; i++) {
		entries[i].first_seen = first_seen(entries[i].name, entries[i].mtime);
	}
	if (seen_changed) {
		save_seen();
	}
	load_today();
	prune_picks();
}

// Prints text with every line pushed 6 spaces to the right, so it sits under
// the title. Prints nothing for an empty note. Used when the output is not a
// terminal, e.g. ./atthing | less
void print_indented(const char *text) {
	if (text[0] == '\0') {
		return;
	}
	printf("      ");
	for (const char *p = text; *p != '\0'; p++) {
		if ((unsigned char)*p < 0x20 && *p != '\n' && *p != '\t') {
			continue;   // skip control bytes, they could mess up the terminal
		}
		putchar(*p);
		if (*p == '\n') {
			printf("      ");
		}
	}
	putchar('\n');
}

// ---- The terminal -----------------------------------------------------------------

struct termios orig_termios;   // terminal settings from before we changed them
int raw_mode_on = 0;

// Writes all n bytes to the screen, even if write() sends them in pieces.
void write_all(const char *s, size_t n) {
	while (n > 0) {
		ssize_t written = write(STDOUT_FILENO, s, n);
		if (written <= 0) {
			return;   // the terminal is gone; nothing we can do
		}
		s += written;
		n -= written;
	}
}

// Puts the terminal back the way we found it. Runs automatically at exit.
void restore_terminal(void) {
	if (!raw_mode_on) {
		return;
	}
	const char *leave = "\x1b[?25h\x1b[?1049l";   // show cursor, leave alternate screen
	write_all(leave, strlen(leave));
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
	raw_mode_on = 0;
}

// Raw mode: every key reaches us immediately (no Enter), and isn't printed.
// Also switches to the alternate screen, so your shell comes back untouched.
int enable_raw_mode(void) {
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
		return -1;
	}
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);   // no echo, no line buffering, no Ctrl-C signal
	raw.c_iflag &= ~(IXON | ICRNL);                    // no Ctrl-S freeze, Enter stays '\r'
	raw.c_cc[VMIN] = 0;    // read() may return with no key...
	raw.c_cc[VTIME] = 1;   // ...after waiting 0.1 seconds
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		return -1;
	}
	raw_mode_on = 1;
	atexit(restore_terminal);

	const char *enter = "\x1b[?1049h\x1b[?25l";   // alternate screen, hide cursor
	write_all(enter, strlen(enter));
	return 0;
}

// Asks the terminal how big it is. Falls back to 24x80 if it won't say.
void get_screen_size(int *rows, int *cols) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_row == 0 || ws.ws_col == 0) {
		*rows = 24;
		*cols = 80;
	} else {
		*rows = ws.ws_row;
		*cols = ws.ws_col;
	}
}

// Special keys get numbers above 255, so they can't be mixed up with letters.
enum {
	KEY_NONE = -1,   // no key within 0.1 seconds
	KEY_GONE = -2,   // the terminal went away
	KEY_ESC = 1000,
	KEY_UP,
	KEY_DOWN,
	KEY_PAGE_UP,
	KEY_PAGE_DOWN
};

// Reads one key press. Arrow keys (and the mouse wheel, in most terminals) arrive
// as a few bytes that start with Escape, like ESC [ A for up. They're joined into
// one key here.
int read_key(void) {
	unsigned char c;
	ssize_t got = read(STDIN_FILENO, &c, 1);
	if (got == -1 && errno != EAGAIN) {
		return KEY_GONE;
	}
	if (got != 1) {
		return KEY_NONE;
	}
	if (c != 27) {
		return c;   // a normal key
	}

	unsigned char kind;
	if (read(STDIN_FILENO, &kind, 1) != 1) {
		return KEY_ESC;   // nothing came after Escape: it was the Esc key itself
	}
	if (kind == 'O') {    // ESC O A: some terminals send arrows like this
		unsigned char final;
		if (read(STDIN_FILENO, &final, 1) != 1) return KEY_NONE;
		if (final == 'A') return KEY_UP;
		if (final == 'B') return KEY_DOWN;
		return KEY_NONE;
	}
	if (kind != '[') {
		return KEY_NONE;   // Alt + a key: not used
	}

	// ESC [, then maybe numbers and ';', then one final letter or '~'
	char params[16];
	int n = 0;
	unsigned char b;
	while (read(STDIN_FILENO, &b, 1) == 1) {
		if (b >= 0x40 && b <= 0x7e) {   // the final byte
			params[n] = '\0';
			if (b == 'A') return KEY_UP;
			if (b == 'B') return KEY_DOWN;
			if (b == '~' && strcmp(params, "5") == 0) return KEY_PAGE_UP;
			if (b == '~' && strcmp(params, "6") == 0) return KEY_PAGE_DOWN;
			return KEY_NONE;   // some other special key
		}
		if (n < (int)sizeof params - 1) {
			params[n++] = b;
		}
	}
	return KEY_NONE;
}

// ---- Drawing ----------------------------------------------------------------------
// The whole screen is built in one buffer, then written at once.
// Writing piece by piece would make the screen flicker.

#define FRAME_SIZE 65536
char frame[FRAME_SIZE];
size_t frame_len = 0;

void frame_add(const char *s, size_t n) {
	if (frame_len + n > FRAME_SIZE) {
		n = FRAME_SIZE - frame_len;   // buffer full: drop the rest
	}
	memcpy(frame + frame_len, s, n);
	frame_len += n;
}

void frame_str(const char *s) {
	frame_add(s, strlen(s));
}

// Moves the cursor. Rows and columns start at 1, top left.
void frame_goto(int row, int col) {
	char code[32];
	snprintf(code, sizeof code, "\x1b[%d;%dH", row, col);
	frame_str(code);
}

// How many bytes the UTF-8 character starting with c takes.
// English letters take 1; Turkish letters like ş take 2; · and … take 3.
int utf8_len(unsigned char c) {
	if (c < 0x80) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;   // broken byte: treat it as one
}

// Length of the character at p, never running past the end of the string.
int char_len(const char *p) {
	int n = utf8_len((unsigned char)*p);
	for (int i = 1; i < n; i++) {
		if (p[i] == '\0') {
			return i;
		}
	}
	return n;
}

// Where draw_text is while it lays text out in rows.
typedef struct {
	int row, col;   // where the first visible row goes on screen
	int skip;       // how many rows of the text are scrolled away above
	int last;       // the last row of the text that fits on screen
	int r;          // which row of the text we're on (0 = first)
	int x;          // how many columns this row already uses
	int wrapped;    // did this row start because the one before was full?
	int cut_x;      // where "…" goes if the text continues past `last`; -1 if it doesn't
} Layout;

int layout_visible(const Layout *l) {
	return l->r >= l->skip && l->r <= l->last;
}

void layout_next_row(Layout *l, int wrapped) {
	if (l->r == l->last) {
		l->cut_x = l->x;   // leaving the last row on screen, and there's more text
	}
	l->r++;
	l->x = 0;
	l->wrapped = wrapped;
	if (layout_visible(l)) {
		frame_goto(l->row + l->r - l->skip, l->col);
	}
}

// Lays text out in rows of `width` columns, wrapping between words, and draws
// rows `skip` to `skip + max_rows - 1` of it at (row, col). If the text goes on
// past that, the last spot shows "…". Returns how many rows the WHOLE text
// needs, so callers can tell whether there's more to scroll to.
int draw_text(const char *text, int row, int col, int width, int max_rows, int skip) {
	if (text[0] == '\0' || width <= 0) {
		return 0;
	}
	Layout l = { row, col, skip, skip + max_rows - 1, 0, 0, 0, -1 };
	if (layout_visible(&l)) {
		frame_goto(row, col);
	}
	const char *p = text;

	while (*p != '\0') {
		if (*p == '\n') {
			layout_next_row(&l, 0);
			p++;
			continue;
		}
		if (*p == ' ' || *p == '\t') {
			if (!(l.x == 0 && l.wrapped) && l.x < width) {   // no spaces at the start of a wrapped row
				if (layout_visible(&l)) frame_str(" ");
				l.x++;
			}
			p++;
			continue;
		}
		if ((unsigned char)*p < 0x20 || *p == 0x7f) {
			p++;   // other control bytes could mess up the terminal; skip them
			continue;
		}

		// measure the next word
		const char *end = p;
		int word_cols = 0;
		while (*end != '\0' && *end != ' ' && *end != '\t' && *end != '\n') {
			end += char_len(end);
			word_cols++;
		}
		// doesn't fit on this row, but would on a fresh one: wrap first
		if (l.x > 0 && l.x + word_cols > width) {
			layout_next_row(&l, 1);
		}
		// print the word, breaking it if it's wider than a whole row
		while (p < end) {
			if (l.x == width) {
				layout_next_row(&l, 1);
			}
			int n = char_len(p);
			if ((unsigned char)*p >= 0x20 && *p != 0x7f) {
				if (layout_visible(&l)) frame_add(p, n);
				l.x++;
			}
			p += n;
		}
	}
	if (l.cut_x >= 0) {
		frame_goto(row + l.last - skip, col + (l.cut_x < width ? l.cut_x : width - 1));
		frame_str("…");
	}
	return l.r + 1;
}

// How many rows text needs at this width. Draws nothing.
int text_rows(const char *text, int width) {
	return draw_text(text, 0, 0, width, 0, 0);
}

// Draws text from its first row, using at most max_rows rows.
// Returns how many rows it used on screen.
int draw_wrapped(const char *text, int row, int col, int width, int max_rows) {
	if (max_rows <= 0) {
		return 0;
	}
	int needed = draw_text(text, row, col, width, max_rows, 0);
	return needed < max_rows ? needed : max_rows;
}

// Draws text in a style: "\x1b[1m" bold, "\x1b[2m" dim. Returns rows used.
int draw_styled(const char *style, const char *text, int row, int col, int width, int max_rows) {
	frame_str(style);
	int used = draw_wrapped(text, row, col, width, max_rows);
	frame_str("\x1b[0m");
	return used;
}

// Starts a new frame. Returns 0 if the screen is big enough to draw on.
int begin_frame(int *rows, int *cols) {
	get_screen_size(rows, cols);
	frame_len = 0;
	frame_str("\x1b[H\x1b[2J");   // cursor to top left, clear the screen
	if (*rows < 8 || *cols < 20) {
		draw_wrapped("terminal too small", 1, 1, *cols, *rows);
		return -1;
	}
	return 0;
}

// "NOW" from "now".
void upper(const char *in, char *out, size_t size) {
	size_t i;
	for (i = 0; in[i] != '\0' && i < size - 1; i++) {
		out[i] = toupper((unsigned char)in[i]);
	}
	out[i] = '\0';
}

// ---- Screens ------------------------------------------------------------------------

enum { VIEW_PICK, VIEW_NOTE, VIEW_DONE, VIEW_FOCUS } view;

int current = 0;          // which of today's picks is on screen
char status[256] = "";    // a one-line message, e.g. an error
char input[64] = "";      // what's typed on the pick screen
int input_len = 0;
int scroll = 0;           // first visible line of the pick list
int body_scroll = 0;      // how many rows of the note's text are scrolled away

// The focus timer. Pausing adds the running part to focus_before.
time_t focus_start;       // when the timer last started or resumed
long focus_before = 0;    // seconds counted before the last pause
int focus_paused = 0;
long focus_shown = -1;    // the second currently on screen

long focus_seconds(void) {
	if (focus_paused) {
		return focus_before;
	}
	return focus_before + (long)difftime(time(NULL), focus_start);
}

// ---- Actions ----------------------------------------------------------------------

// Adds one line to .atthing/log, e.g. "2026-09-10 14:22  done   now/cubesat.md".
// `extra` goes at the end of the line; pass "" for nothing.
void log_action(const char *action, const Entry *e, const char *extra) {
	char path[1024], stamp[32];
	snprintf(path, sizeof path, "%s/log", state_dir);
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (tm == NULL || strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", tm) == 0) {
		snprintf(stamp, sizeof stamp, "0000-00-00 00:00");
	}
	FILE *f = fopen(path, "a");   // "a": add to the end, create the file if needed
	if (f == NULL) {
		return;
	}
	fprintf(f, "%s  %-5s  %s/%s%s%s\n", stamp, action, e->bucket, e->name,
	        extra[0] != '\0' ? "  " : "", extra);
	fclose(f);
}

// Moves a note into the folder `to` (inside base), creating the folder if needed.
// Never overwrites: if the name is taken there, it becomes "name (2).md", "name (3).md"...
// Returns 0 if it worked, -1 if not (errno says why).
int move_note(const Entry *e, const char *to) {
	char dir[1024], dest[1400];
	snprintf(dir, sizeof dir, "%s/%s", base, to);
	if (ensure_dir(dir) == -1) {
		return -1;
	}
	snprintf(dest, sizeof dest, "%s/%s", dir, e->name);

	const char *dot = strrchr(e->name, '.');
	int stem = (dot != NULL) ? (int)(dot - e->name) : (int)strlen(e->name);
	struct stat st;
	for (int i = 2; stat(dest, &st) == 0; i++) {   // stat() == 0 means the name is taken
		if (i > 99) {
			errno = EEXIST;
			return -1;
		}
		snprintf(dest, sizeof dest, "%s/%.*s (%d)%s", dir, stem, e->name, i, dot != NULL ? dot : "");
	}
	return rename(e->path, dest);
}

// Parks a note: moves it to someday/ and adds "- title (parked 2026-09-10)" to
// Parked.md. Parked is a status, not a lost note.
int park_note(const Entry *e) {
	if (move_note(e, "someday") == -1) {
		return -1;
	}
	char path[1024], date[16];
	snprintf(path, sizeof path, "%s/Parked.md", base);
	date_string(time(NULL), date, sizeof date);

	FILE *f = fopen(path, "a+");   // read and add to the end
	if (f == NULL) {
		return -1;
	}
	// if the file doesn't end with a newline, add one first
	if (fseek(f, -1, SEEK_END) == 0 && fgetc(f) != '\n') {
		fseek(f, 0, SEEK_END);
		fputc('\n', f);
	}
	fseek(f, 0, SEEK_END);
	fprintf(f, "- %s (parked %s)\n", e->title, date);
	return fclose(f) == 0 ? 0 : -1;
}

// Removes picks[i]; the picks after it move up by one.
void remove_pick(int i) {
	for (int j = i; j < n_picks - 1; j++) {
		memcpy(picks[j], picks[j + 1], sizeof picks[j]);
	}
	n_picks--;
}

// Finishes the pick on screen: done (to Done/) or parked (to someday/).
// Returns 0 if it worked, -1 with a message in `status` if not.
int finish_pick(int park) {
	int index = find_entry(picks[current]);
	if (index == -1) {
		return -1;
	}
	Entry e = entries[index];   // a copy, because reload() below refills entries[]

	int result = park ? park_note(&e) : move_note(&e, "Done");
	if (result == -1) {
		snprintf(status, sizeof status, "couldn't move it: %s", strerror(errno));
		return -1;
	}
	log_action(park ? "park" : "done", &e, "");
	if (!park) {
		done_today++;
	}
	remove_pick(current);
	save_today();
	reload();   // find the notes again, now that one has moved
	if (current >= n_picks) {
		current = 0;
	}
	body_scroll = 0;
	view = (n_picks == 0) ? VIEW_DONE : VIEW_NOTE;
	return 0;
}

// Draws a note's text in the rows it has, scrolled by body_scroll, plus a hint
// on `hint_row` when it doesn't all fit.
void draw_body(const Entry *e, int top, int rows, int left, int width, int hint_row) {
	int needed = text_rows(e->body, width);
	int most = needed - rows;          // the furthest it can scroll
	if (body_scroll > most) body_scroll = most;
	if (body_scroll < 0) body_scroll = 0;
	draw_text(e->body, top, left, width, rows, body_scroll);

	if (needed > rows && status[0] == '\0') {
		char hint[64];
		int below = needed - (body_scroll + rows);
		if (below > 0) {
			snprintf(hint, sizeof hint, "j/k scroll · %d more line%s", below, below == 1 ? "" : "s");
		} else {
			snprintf(hint, sizeof hint, "j/k scroll");
		}
		draw_styled("\x1b[2m", hint, hint_row, left, width, 1);
	}
}

void draw_pick_screen(void) {
	int rows, cols;
	if (begin_frame(&rows, &cols) == -1) {
		write_all(frame, frame_len);
		return;
	}
	int left = 3;
	int width = cols - 4;

	char head[64];
	snprintf(head, sizeof head, "PICK UP TO %d · done %d/%d", MAX_PICKS, done_today, DAILY_GOAL);
	draw_styled("\x1b[2m", head, 2, left, width, 1);

	int list_top = 4;
	int list_rows = rows - 4 - list_top;   // leave room for status, input, keys
	if (n_entries == 0) {
		draw_wrapped("nothing in now/ or try/", list_top, left, width, 1);
	}
	if (scroll > n_entries - list_rows) scroll = n_entries - list_rows;
	if (scroll < 0) scroll = 0;

	for (int i = scroll; i < n_entries && i - scroll < list_rows; i++) {
		char number[12], bucket[16];
		snprintf(number, sizeof number, "%3d", i + 1);
		upper(entries[i].bucket, bucket, sizeof bucket);
		int row = list_top + (i - scroll);
		draw_wrapped(number, row, left, 3, 1);
		draw_styled("\x1b[2m", bucket, row, left + 5, 3, 1);
		draw_wrapped(is_picked(i) ? "*" : " ", row, left + 9, 1, 1);
		draw_wrapped(entries[i].title, row, left + 11, width - 11, 1);
	}
	if (scroll + list_rows < n_entries) {
		draw_styled("\x1b[2m", "↓ more", list_top + list_rows, left + 5, width - 5, 1);
	}

	if (status[0] != '\0') {
		draw_styled("\x1b[2m", status, rows - 3, left, width, 1);
	}
	char prompt[128];
	snprintf(prompt, sizeof prompt, "type numbers, then Enter: %s_", input);
	draw_wrapped(prompt, rows - 2, left, width, 1);
	char keys[64];
	snprintf(keys, sizeof keys, "%s%s[q]uit",
	         n_entries > list_rows ? "[j/k] scroll  " : "",
	         (n_picks > 0 || done_today > 0) ? "[Esc] back  " : "");
	draw_wrapped(keys, rows, left, width, 1);

	write_all(frame, frame_len);
}

void draw_note_screen(void) {
	int rows, cols;
	if (begin_frame(&rows, &cols) == -1) {
		write_all(frame, frame_len);
		return;
	}
	int left = 3;
	int width = cols - 4;

	int index = find_entry(picks[current]);
	if (index == -1) {
		write_all(frame, frame_len);
		return;
	}
	Entry *e = &entries[index];

	// header: NOW · 12 days · 2 of 3 · done 1/3
	char bucket[16], age[32], header[160];
	upper(e->bucket, bucket, sizeof bucket);
	long days = days_between(e->first_seen, time(NULL));
	if (days <= 0) {
		snprintf(age, sizeof age, "today");
	} else if (days == 1) {
		snprintf(age, sizeof age, "1 day");
	} else {
		snprintf(age, sizeof age, "%ld days", days);
	}
	snprintf(header, sizeof header, "%s · %s · %d of %d · done %d/%d",
	         bucket, age, current + 1, n_picks, done_today, DAILY_GOAL);
	draw_styled("\x1b[2m", header, 2, left, width, 1);

	int title_rows = draw_styled("\x1b[1m", e->title, 4, left, width, 2);

	// body: everything between the title and the bottom lines
	int body_top = 4 + title_rows + 1;
	int body_rows = (rows - 3) - body_top;
	draw_body(e, body_top, body_rows, left, width, rows - 2);

	if (status[0] != '\0') {
		draw_styled("\x1b[2m", status, rows - 2, left, width, 1);
	}
	draw_wrapped(width >= 47 ? "[d]one  [s]kip  [p]ark  [f]ocus  [l]ist  [q]uit"
	                         : "[d]one [s]kip [p]ark [f]ocus [l]ist [q]uit",   // narrow window
	             rows, left, width, 1);

	write_all(frame, frame_len);
}

// Shown when every pick is done or parked.
void draw_done_screen(void) {
	int rows, cols;
	if (begin_frame(&rows, &cols) == -1) {
		write_all(frame, frame_len);
		return;
	}
	int left = 3;
	int width = cols - 4;
	char line[64];
	snprintf(line, sizeof line, "done %d/%d", done_today, DAILY_GOAL);
	draw_styled("\x1b[1m", line, 4, left, width, 1);
	draw_wrapped(n_entries == 0 ? "nothing left in now/ or try/" : "nothing picked right now",
	             6, left, width, 1);
	if (status[0] != '\0') {
		draw_styled("\x1b[2m", status, rows - 2, left, width, 1);
	}
	draw_wrapped("[l]ist  [q]uit", rows, left, width, 1);
	write_all(frame, frame_len);
}

// Focus mode: only the note and the timer.
void draw_focus_screen(void) {
	int rows, cols;
	if (begin_frame(&rows, &cols) == -1) {
		write_all(frame, frame_len);
		return;
	}
	int left = 3;
	int width = cols - 4;
	int index = find_entry(picks[current]);
	if (index == -1) {
		write_all(frame, frame_len);
		return;
	}
	Entry *e = &entries[index];

	draw_styled("\x1b[2m", focus_paused ? "FOCUS · paused" : "FOCUS", 2, left, width, 1);
	int title_rows = draw_styled("\x1b[1m", e->title, 4, left, width, 2);
	int body_top = 4 + title_rows + 1;
	int body_rows = (rows - 5) - body_top;
	draw_body(e, body_top, body_rows, left, width, rows - 2);

	focus_shown = focus_seconds();
	char timer[32];
	format_duration(focus_shown, timer, sizeof timer);
	int timer_col = (cols - (int)strlen(timer)) / 2 + 1;   // centered
	draw_styled("\x1b[1m", timer, rows - 3, timer_col, width, 1);

	draw_wrapped(focus_paused ? "[space] resume  [f] stop  [d]one"
	                          : "[space] pause  [f] stop  [d]one",
	             rows, left, width, 1);
	write_all(frame, frame_len);
}

void draw_screen(void) {
	if (view == VIEW_PICK) {
		draw_pick_screen();
	} else if (view == VIEW_NOTE) {
		draw_note_screen();
	} else if (view == VIEW_FOCUS) {
		draw_focus_screen();
	} else {
		draw_done_screen();
	}
}

// Reads "1 4 6" from the pick screen and makes those today's picks.
// Returns 0 if it worked, -1 with a message in `status` if not.
int confirm_picks(void) {
	int chosen[MAX_PICKS];
	int count = 0;
	const char *p = input;

	while (*p != '\0') {
		if (*p == ' ' || *p == ',') {
			p++;
			continue;
		}
		char *end;
		long number = strtol(p, &end, 10);   // reads the number starting at p
		if (end == p) {
			p++;
			continue;
		}
		p = end;
		if (number < 1 || number > n_entries) {
			snprintf(status, sizeof status, "there's no note %ld", number);
			return -1;
		}
		int duplicate = 0;
		for (int i = 0; i < count; i++) {
			if (chosen[i] == number - 1) duplicate = 1;
		}
		if (duplicate) {
			continue;
		}
		if (count == MAX_PICKS) {
			snprintf(status, sizeof status, "up to %d", MAX_PICKS);
			return -1;
		}
		chosen[count++] = (int)number - 1;
	}
	if (count == 0) {
		snprintf(status, sizeof status, "type at least one number");
		return -1;
	}

	for (int i = 0; i < count; i++) {
		if (!is_picked(chosen[i])) {
			log_action("pick", &entries[chosen[i]], "");   // only log what's new
		}
	}
	n_picks = count;
	for (int i = 0; i < count; i++) {
		snprintf(picks[i], sizeof picks[i], "%s/%s", entries[chosen[i]].bucket, entries[chosen[i]].name);
	}
	save_today();
	return 0;
}

// Opens the pick screen with today's picks already typed in, so they're easy
// to change: Backspace one away, type another, Enter.
void open_list(void) {
	input_len = 0;
	input[0] = '\0';
	for (int i = 0; i < n_entries; i++) {
		if (is_picked(i)) {
			char number[16];
			snprintf(number, sizeof number, "%d ", i + 1);
			size_t len = strlen(number);
			if ((size_t)input_len + len < sizeof input) {
				memcpy(input + input_len, number, len + 1);
				input_len += (int)len;
			}
		}
	}
	scroll = 0;
	view = VIEW_PICK;
}

// A key on the pick screen. Returns 1 to quit.
int pick_key(int key) {
	status[0] = '\0';
	if (key == 'q' || key == 3) {   // 3 is Ctrl-C
		return 1;
	} else if (key == 'j' || key == KEY_DOWN) {
		scroll++;
	} else if (key == 'k' || key == KEY_UP) {
		scroll--;
	} else if (key == KEY_PAGE_DOWN) {
		scroll += 10;
	} else if (key == KEY_PAGE_UP) {
		scroll -= 10;
	} else if (key == KEY_ESC || key == 'l') {
		input_len = 0;   // back without changing anything
		input[0] = '\0';
		if (n_picks > 0) {
			view = VIEW_NOTE;
		} else if (done_today > 0) {
			view = VIEW_DONE;
		}
	} else if ((key >= '0' && key <= '9') || key == ' ' || key == ',') {
		if (input_len < (int)sizeof input - 1) {
			input[input_len++] = (char)key;
			input[input_len] = '\0';
		}
	} else if ((key == 127 || key == 8) && input_len > 0) {   // Backspace
		input[--input_len] = '\0';
	} else if (key == '\r' || key == '\n') {
		if (confirm_picks() == 0) {
			input_len = 0;
			input[0] = '\0';
			current = 0;
			body_scroll = 0;
			view = VIEW_NOTE;
		}
	}
	return 0;
}

// j/k, arrows and Page Up/Down scroll a long note. Returns 1 if the key was one of those.
int scroll_body_key(int key) {
	if (key == 'j' || key == KEY_DOWN) {
		body_scroll++;
	} else if (key == 'k' || key == KEY_UP) {
		body_scroll--;
	} else if (key == KEY_PAGE_DOWN) {
		body_scroll += 10;
	} else if (key == KEY_PAGE_UP) {
		body_scroll -= 10;
	} else {
		return 0;
	}
	return 1;   // draw_body keeps body_scroll in range
}

// A key on the note screen. Returns 1 to quit.
int note_key(int key) {
	status[0] = '\0';
	if (key == 'q' || key == 3) {
		return 1;
	} else if (scroll_body_key(key)) {
		// scrolled the note
	} else if (key == 'l') {
		open_list();
	} else if (key == 's') {
		current = (current + 1) % n_picks;   // after the last pick, back to the first
		body_scroll = 0;
	} else if (key == 'f') {
		focus_before = 0;
		focus_start = time(NULL);
		focus_paused = 0;
		view = VIEW_FOCUS;
	} else if (key == 'd') {
		finish_pick(0);
	} else if (key == 'p') {
		finish_pick(1);
	}
	return 0;
}

// Stops the timer and writes the time spent into the log.
void stop_focus(void) {
	int index = find_entry(picks[current]);
	if (index != -1) {
		char spent[32];
		format_duration(focus_seconds(), spent, sizeof spent);
		log_action("focus", &entries[index], spent);
	}
	view = VIEW_NOTE;
}

// A key in focus mode. Returns 1 to quit.
int focus_key(int key) {
	status[0] = '\0';
	if (key == 'q' || key == 3) {
		stop_focus();
		return 1;
	} else if (scroll_body_key(key)) {
		// scrolled the note
	} else if (key == ' ') {
		if (focus_paused) {
			focus_start = time(NULL);   // resume: count from now
			focus_paused = 0;
		} else {
			focus_before = focus_seconds();   // pause: keep what we have so far
			focus_paused = 1;
		}
	} else if (key == 'f') {
		stop_focus();
	} else if (key == 'd') {
		stop_focus();
		finish_pick(0);
	}
	return 0;
}

// A key on the "done" screen. Returns 1 to quit.
int done_key(int key) {
	status[0] = '\0';
	if (key == 'q' || key == 3) {
		return 1;
	} else if (key == 'l' || key == 'n') {
		open_list();
	}
	return 0;
}

// The full-screen loop: draw, wait for a key, react, repeat.
int run_screen(int force_pick) {
	if (ensure_dir(state_dir) == -1) {
		fprintf(stderr, "atthing: can't create %s: %s\n", state_dir, strerror(errno));
		return 1;
	}
	reload();
	if (force_pick) {
		view = VIEW_PICK;
	} else if (n_picks > 0) {
		view = VIEW_NOTE;
	} else if (done_today > 0) {
		view = VIEW_DONE;   // came back after finishing today's picks
	} else {
		view = VIEW_PICK;
	}

	if (enable_raw_mode() == -1) {
		perror("atthing: can't set up the terminal");
		return 1;
	}

	int last_rows = 0, last_cols = 0;
	int redraw = 1;

	for (;;) {
		int rows, cols;
		get_screen_size(&rows, &cols);
		if (rows != last_rows || cols != last_cols) {   // window was resized
			last_rows = rows;
			last_cols = cols;
			redraw = 1;
		}
		if (view == VIEW_FOCUS && focus_seconds() != focus_shown) {
			redraw = 1;   // the timer moved on by a second
		}
		if (redraw) {
			draw_screen();
			redraw = 0;
		}

		int key = read_key();
		if (key == KEY_GONE) {
			return 1;   // the terminal went away
		}
		if (key == KEY_NONE) {
			continue;   // no key within 0.1 seconds; check the size again
		}

		int quit;
		if (view == VIEW_PICK) {
			quit = pick_key(key);
		} else if (view == VIEW_NOTE) {
			quit = note_key(key);
		} else if (view == VIEW_FOCUS) {
			quit = focus_key(key);
		} else {
			quit = done_key(key);
		}
		if (quit) {
			break;
		}
		redraw = 1;
	}
	return 0;
}

void print_usage(FILE *out) {
	fprintf(out,
		"atthing " VERSION " - one thing at a time\n"
		"\n"
		"usage:\n"
		"  atthing            open it (the first time each day you pick up to 3)\n"
		"  atthing pick       pick again\n"
		"  atthing --help     this text\n"
		"  atthing --version  the version\n"
		"\n"
		"notes folder: $ATTHING_DIR, or ~/Life/forgetme if it isn't set.\n"
		"it needs now/ and try/ inside. atthing keeps its memory in .atthing/ there.\n"
		"\n"
		"keys:\n"
		"  picking   type numbers + Enter, j/k or arrows scroll, Esc back, q quit\n"
		"  a note    d done, s skip, p park, f focus, l list, j/k scroll, q quit\n"
		"  focus     space pause, f stop, d done, j/k scroll\n");
}

int main(int argc, char **argv) {
	int force_pick = 0;
	if (argc > 1) {
		if (strcmp(argv[1], "pick") == 0) {
			force_pick = 1;   // "atthing pick": choose again
		} else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
			print_usage(stdout);
			return 0;
		} else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
			printf("atthing %s\n", VERSION);
			return 0;
		} else {
			fprintf(stderr, "atthing: unknown option '%s'\n\n", argv[1]);
			print_usage(stderr);
			return 2;
		}
	}

	const char *env = getenv("ATTHING_DIR");
	if (env != NULL) {
		snprintf(base, sizeof base, "%s", env);
	} else {
		const char *home = getenv("HOME");
		if (home == NULL) {
			fprintf(stderr, "atthing: HOME is not set\n");
			return 1;
		}
		snprintf(base, sizeof base, "%s/Life/forgetme", home);
	}
	snprintf(state_dir, sizeof state_dir, "%s/.atthing", base);

	if (scan_notes() == 0) {
		fprintf(stderr, "atthing: no now/ or try/ folder in %s\n", base);
		return 1;
	}

	// in a real terminal: the full screen
	if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
		return run_screen(force_pick);
	}

	// output going somewhere else (a file, a pipe): print a plain list
	for (int i = 0; i < n_entries; i++) {
		printf("[%s] %s\n", entries[i].bucket, entries[i].title);
		print_indented(entries[i].body);
	}
	return 0;
}
