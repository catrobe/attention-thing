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

#define MAX_ENTRIES 128
#define MAX_BODY    4096  // longest note text we keep, in bytes
#define MAX_PICKS   3     // how many notes you pick at a time
#define MAX_SEEN    1024  // how many first-seen dates we remember

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

// Draws text starting at (row, col), wrapping between words at `width`
// columns and using at most `max_rows` rows. If it doesn't all fit, the last
// visible spot shows "…". Returns how many rows it used.
int draw_wrapped(const char *text, int row, int col, int width, int max_rows) {
	if (text[0] == '\0' || width <= 0 || max_rows <= 0) {
		return 0;
	}
	int r = 0;         // which row we're on, counting from 0
	int x = 0;         // how many columns are used on this row
	int wrapped = 0;   // did we just move to a new row because of width?
	int cut = 0;       // ran out of rows before the text ended
	const char *p = text;

	frame_goto(row, col);
	while (*p != '\0') {
		if (*p == '\n') {
			if (r + 1 >= max_rows) { cut = 1; break; }
			r++; x = 0; wrapped = 0;
			frame_goto(row + r, col);
			p++;
			continue;
		}
		if (*p == ' ' || *p == '\t') {
			if (!(x == 0 && wrapped) && x < width) {   // no spaces at the start of a wrapped row
				frame_str(" ");
				x++;
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
		if (x > 0 && x + word_cols > width) {
			if (r + 1 >= max_rows) { cut = 1; break; }
			r++; x = 0; wrapped = 1;
			frame_goto(row + r, col);
		}
		// print the word, breaking it if it's wider than a whole row
		while (p < end) {
			if (x == width) {
				if (r + 1 >= max_rows) { cut = 1; break; }
				r++; x = 0; wrapped = 1;
				frame_goto(row + r, col);
			}
			int n = char_len(p);
			if ((unsigned char)*p >= 0x20 && *p != 0x7f) {
				frame_add(p, n);
				x++;
			}
			p += n;
		}
		if (cut) {
			break;
		}
	}
	if (cut) {
		frame_goto(row + r, col + (x < width ? x : width - 1));
		frame_str("…");
	}
	return r + 1;
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

enum { VIEW_PICK, VIEW_NOTE } view;

int current = 0;          // which of today's picks is on screen
char status[256] = "";    // a one-line message, e.g. an error
char input[64] = "";      // what's typed on the pick screen
int input_len = 0;
int scroll = 0;           // first visible line of the pick list

void draw_pick_screen(void) {
	int rows, cols;
	if (begin_frame(&rows, &cols) == -1) {
		write_all(frame, frame_len);
		return;
	}
	int left = 3;
	int width = cols - 4;

	draw_styled("\x1b[2m", "PICK UP TO 3", 2, left, width, 1);

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
	draw_wrapped("[j/k] scroll  [q]uit", rows, left, width, 1);

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

	// header: NOW · 12 days · 2 of 3
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
	snprintf(header, sizeof header, "%s · %s · %d of %d", bucket, age, current + 1, n_picks);
	draw_styled("\x1b[2m", header, 2, left, width, 1);

	int title_rows = draw_styled("\x1b[1m", e->title, 4, left, width, 2);

	// body: everything between the title and the bottom lines
	int body_top = 4 + title_rows + 1;
	int body_rows = (rows - 3) - body_top;
	draw_wrapped(e->body, body_top, left, width, body_rows);

	if (status[0] != '\0') {
		draw_styled("\x1b[2m", status, rows - 2, left, width, 1);
	}
	draw_wrapped("[d]one  [s]kip  [p]ark  [q]uit", rows, left, width, 1);

	write_all(frame, frame_len);
}

void draw_screen(void) {
	if (view == VIEW_PICK) {
		draw_pick_screen();
	} else {
		draw_note_screen();
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

	n_picks = count;
	for (int i = 0; i < count; i++) {
		snprintf(picks[i], sizeof picks[i], "%s/%s", entries[chosen[i]].bucket, entries[chosen[i]].name);
	}
	save_today();
	return 0;
}

// A key on the pick screen. Returns 1 to quit.
int pick_key(char c) {
	status[0] = '\0';
	if (c == 'q' || c == 3) {   // 3 is Ctrl-C
		return 1;
	} else if (c == 'j') {
		scroll++;
	} else if (c == 'k') {
		scroll--;
	} else if ((c >= '0' && c <= '9') || c == ' ' || c == ',') {
		if (input_len < (int)sizeof input - 1) {
			input[input_len++] = c;
			input[input_len] = '\0';
		}
	} else if ((c == 127 || c == 8) && input_len > 0) {   // Backspace
		input[--input_len] = '\0';
	} else if (c == '\r' || c == '\n') {
		if (confirm_picks() == 0) {
			input_len = 0;
			input[0] = '\0';
			current = 0;
			view = VIEW_NOTE;
		}
	}
	return 0;
}

// A key on the note screen. Returns 1 to quit.
int note_key(char c) {
	status[0] = '\0';
	if (c == 'q' || c == 3) {
		return 1;
	} else if (c == 's') {
		current = (current + 1) % n_picks;   // after the last pick, back to the first
	} else if (c == 'd' || c == 'p') {
		snprintf(status, sizeof status, "not yet: this key works in a later step");
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
	view = (force_pick || n_picks == 0) ? VIEW_PICK : VIEW_NOTE;

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
		if (redraw) {
			draw_screen();
			redraw = 0;
		}

		char c;
		ssize_t got = read(STDIN_FILENO, &c, 1);
		if (got == -1 && errno != EAGAIN) {
			return 1;   // the terminal went away
		}
		if (got != 1) {
			continue;   // no key within 0.1 seconds; check the size again
		}

		int quit = (view == VIEW_PICK) ? pick_key(c) : note_key(c);
		if (quit) {
			break;
		}
		redraw = 1;
	}
	return 0;
}

int main(int argc, char **argv) {
	int force_pick = 0;
	if (argc > 1) {
		if (strcmp(argv[1], "pick") == 0) {
			force_pick = 1;   // "atthing pick": choose again
		} else {
			fprintf(stderr, "usage: atthing [pick]\n");
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
