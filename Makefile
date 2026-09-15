CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11

shell: shell.c
	$(CC) $(CFLAGS) -o shell shell.c

clean:
	rm -f shell

.PHONY: clean