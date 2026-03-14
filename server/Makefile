# Makefile for aesdsocket server. Use standard targets for compiling
# and standard variables for compiler and flags. Use the -g flag to include
# debugging symbols in the executable.

CC = gcc
CFLAGS = -g -Wall -Werror

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o $@ $^

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f aesdsocket aesdsocket.o