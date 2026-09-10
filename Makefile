CFLAGS = -Wall -Wextra -std=c11 -g

atthing: main.c
	$(CC) $(CFLAGS) main.c -o atthing

clean:
	rm -f atthing atthing-asan
