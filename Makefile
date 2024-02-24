
all: ed

ed: ed.c
	$(CC) ed.c -o ed -g -Wall -Wextra -std=c99
