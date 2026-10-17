CFLAGS = -Wall -Wextra -std=c11 -g
PREFIX ?= $(HOME)/.local

atthing: main.c
	$(CC) $(CFLAGS) main.c -o atthing

# After installing: if the shell can't find atthing there, say how to fix it.
install: atthing
	install -Dm755 atthing $(PREFIX)/bin/atthing
	@case ":$$PATH:" in *":$(PREFIX)/bin:"*) ;; *) \
		echo; \
		echo "$(PREFIX)/bin is not in your PATH, so typing atthing won't find it yet."; \
		echo "add this line to ~/.bashrc (or ~/.zshrc), then open a new terminal:"; \
		echo; \
		echo '    export PATH="$(PREFIX)/bin:$$PATH"'; \
		echo; \
		echo "or install it for everyone: sudo make install PREFIX=/usr/local"; \
	esac

uninstall:
	rm -f $(PREFIX)/bin/atthing

clean:
	rm -f atthing atthing-asan

.PHONY: install uninstall clean
