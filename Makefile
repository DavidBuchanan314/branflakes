
CC=gcc
CFLAGS=-std=gnu99 -O3 -Wall
TARGET_ARCH=-m64

main: main.o
	$(CC) $(CFLAGS) $(TARGET_ARCH) main.o -o main

clean:
	-rm -f main main.o
