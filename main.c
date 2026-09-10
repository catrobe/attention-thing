// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 M. Ömer Okyar

#include <stdio.h>    // printf, snprintf, perror
#include <stdlib.h>   // getenv
#include <string.h>   // strlen, strcmp
#include <time.h>     // time_t
#include <dirent.h>   // oepndir, readdir, closedir
#include <sys/stat.h> // stat

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
// the title. Prints nothing for an empty note. (Temporary: Step 4 replaces
// this with a real screen.)
void print_indented(const char *text) {
	if (text[0] == '\0') {
		return;
	}
	printf("      ");
	for (const char *p = text; *p != '\0'; p++) {
		putchar(*p);
		if (*p == '\n') {
			printf("      ");
		}
	}
	putchar('\n');
}

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

	for (int i = 0; i < n_entries; i++) {
		printf("[%s] %s\n", entries[i].bucket, entries[i].title);
		print_indented(entries[i].body);
	}

	return 0;
}
