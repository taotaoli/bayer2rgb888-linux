CC=gcc
#CFLAGS=-std=c99

ifdef DEBUG
  CFLAGS += -ggdb -Wall -DDEBUG
else
  CFLAGS += -O3 -Wall
endif


all: bayer2rgb
	
bayer2rgb: main.c bayer.o
	$(CC) $(CFLAGS) -o bayer2rgb bayer.o main.c -lm
ifdef RELEASE
	strip bayer2rgb
endif

bayer.o: bayer.c
	$(CC) $(CFLAGS) -std=c99 -c bayer.c

clean:
	-rm -rf bayer2rgb *.o *.tif
