CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11
LDFLAGS = -lreadline

SRCS = mishell.c parser.c exec.c jobs.c builtins.c pmon.c
OBJS = $(SRCS:.c=.o)

mishell: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f mishell $(OBJS)

.PHONY: mishell clean