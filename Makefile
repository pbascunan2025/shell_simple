CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11
LDFLAGS = -lreadline

SRCS = shell.c parser.c exec.c jobs.c builtins.c pmon.c
OBJS = $(SRCS:.c=.o)

shell: $(OBJS)
	$(CC) $(CFLAGS) -o mishell $(OBJS) $(LDFLAGS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f mishell $(OBJS)

.PHONY: clean