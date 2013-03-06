CC= gcc
CFLAGS= -g -Wall -std=c99

all: shell
	$(CC) -o shell shell.c $(CFLAGS)
clean:
	rm -f shell shell.o *.core core *~
