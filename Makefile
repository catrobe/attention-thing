CFLAGS = -Wall -Wextra -std=c11 -g
PREFIX ?= $(HOME)/.local

atthing: main.c
	$(CC) $(CFLAGS) main.c -o atthing

install: atthing
	install -Dm755 atthing $(PREFIX)/bin/atthing

uninstall:
	rm -f $(PREFIX)/bin/atthing

clean:
	rm -f atthing atthing-asan

.PHONY: install uninstall clean
