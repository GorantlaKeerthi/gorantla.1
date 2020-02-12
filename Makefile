CC=gcc
CFLAGS=-g

bt: bt.c
	$(CC) $(CFLAGS) bt.c -o bt

clean:
	rm bt
