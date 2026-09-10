// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 M. Ömer Okyar

#include <ctype.h>    // toupper
#include <errno.h>    // errno, EAGAIN
#include <stdio.h>    // printf, snprintf, perror
#include <stdlib.h>   // getenv
#include <string.h>   // strlen, strcmp
#include <time.h>     // time_t
#include <dirent.h>   // oepndir, readdir, closedir
#include <sys/stat.h> // stat
#include <sys/ioctl.h> // ioctl, TIOCGWINSZ: terminal size
#include <termios.h>  // tcgetattr, tcsetattr: raw mode
#include <unistd.h>   // read, write, isatty

#define MAX_ENTRIES 128
#define MAX_BODY    4096  // longest note text we keep, in bytes

typedef struct {
	char   path[1024];  // full path to the .md files
	char   title[256];  // file names without ".md"
	char   bucket[16];  // "now" or "try"
	time_t mtime;       // last modified, in seconds since 1970
	char   body[MAX_BODY]; // text inside the note, "" if the note is empty
} Entry;

Entry entries[MAX_ENTRIES];
int n_entries = 0;

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

// ---- The terminal -----------------------------------------------------------

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

// ---- Drawing ----------------------------------------------------------------
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

// Builds and shows the screen for entries[current].
void draw_screen(int current, const char *status) {
	int rows, cols;
	get_screen_size(&rows, &cols);
	Entry *e = &entries[current];

	frame_len = 0;
	frame_str("\x1b[H\x1b[2J");   // cursor to top left, clear the screen

	int left = 3;
	int width = cols - 4;
	if (rows < 8 || width < 16) {
		draw_wrapped("terminal too small", 1, 1, cols, rows);
		write_all(frame, frame_len);
		return;
	}

	// header: NOW · 12 days · 3 of 9
	char bucket_upper[16];
	size_t i;
	for (i = 0; e->bucket[i] != '\0' && i < sizeof bucket_upper - 1; i++) {
		bucket_upper[i] = toupper((unsigned char)e->bucket[i]);
	}
	bucket_upper[i] = '\0';

	char age[32] = "";
	if (e->mtime > 0) {
		long days = (long)((time(NULL) - e->mtime) / 86400);
		if (days <= 0) {
			snprintf(age, sizeof age, " · today");
		} else if (days == 1) {
			snprintf(age, sizeof age, " · 1 day");
		} else {
			snprintf(age, sizeof age, " · %ld days", days);
		}
	}

	char header[128];
	snprintf(header, sizeof header, "%s%s · %d of %d", bucket_upper, age, current + 1, n_entries);
	frame_str("\x1b[2m");   // dim
	draw_wrapped(header, 2, left, width, 1);
	frame_str("\x1b[0m");

	// title, bold, at most 2 rows
	frame_str("\x1b[1m");
	int title_rows = draw_wrapped(e->title, 4, left, width, 2);
	frame_str("\x1b[0m");

	// body: everything between the title and the bottom lines
	int body_top = 4 + title_rows + 1;
	int body_rows = (rows - 3) - body_top;
	draw_wrapped(e->body, body_top, left, width, body_rows);

	// status message, then the keys on the last row
	if (status[0] != '\0') {
		frame_str("\x1b[2m");
		draw_wrapped(status, rows - 2, left, width, 1);
		frame_str("\x1b[0m");
	}
	draw_wrapped("[d]one  [s]kip  [p]ark  [q]uit", rows, left, width, 1);

	write_all(frame, frame_len);
}

// The full-screen loop: draw, wait for a key, react, repeat.
int run_screen(void) {
	if (enable_raw_mode() == -1) {
		perror("atthing: can't set up the terminal");
		return 1;
	}

	int current = 0;
	const char *status = "";
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
			draw_screen(current, status);
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

		status = "";
		if (c == 'q' || c == 3) {   // 3 is Ctrl-C
			break;
		} else if (c == 's') {
			current = (current + 1) % n_entries;   // after the last note, back to the first
		} else if (c == 'd' || c == 'p') {
			status = "not yet: this key works in a later step";
		}
		redraw = 1;
	}
	return 0;
}

// ---- Finding notes --------------------------------------------------------

// Adds every .md file inside base/bucket to entries[].
void bucket_scan(const char *base, const char *bucket) {
	char dir[512];
	snprintf(dir, sizeof dir, "%s/%s", base, bucket);

	DIR *d = opendir(dir);
	if (d == NULL) {
		perror(dir);
		return;
	}

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (!ends_with(ent->d_name, ".md")) {
			continue;   // skip ".", "..", and anything that isn't a note
		}
		if (n_entries >= MAX_ENTRIES) {
			break;      // array is full, stop instead of writing past the end
		}

		Entry *e = &entries[n_entries];   // e points at the next free slot

		snprintf(e->path, sizeof e->path, "%s/%s", dir, ent->d_name);
		snprintf(e->bucket, sizeof e->bucket, "%s", bucket);
		read_body(e->path, e->body, sizeof e->body);

		snprintf(e->title, sizeof e->title, "%s", ent->d_name);
		e->title[strlen(e->title) - 3] = '\0';   // cut off ".md"

		struct stat st;
		if (stat(e->path, &st) == 0) {
			e->mtime = st.st_mtime;
		} else {
			e->mtime = 0;
		}

		n_entries++;
	}

	closedir(d);
}

int main(void) {
	char base[512];
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

	bucket_scan(base, "now");
	bucket_scan(base, "try");

	if (n_entries == 0) {
		fprintf(stderr, "atthing: no .md files found in %s\n", base);
		return 1;
	}

	// in a real terminal: the full screen
	if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
		return run_screen();
	}

	// output going somewhere else (a file, a pipe): print a plain list
	for (int i = 0; i < n_entries; i++) {
		printf("[%s] %s\n", entries[i].bucket, entries[i].title);
		print_indented(entries[i].body);
	}

	return 0;
}
