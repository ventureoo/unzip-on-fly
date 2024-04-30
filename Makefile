
CFLAGS_CURL = $(shell curl-config --cflags)
LDFLAGS_CURL = $(shell curl-config --libs)
CFLAGS_ARCHIVE = $(shell pkg-config --cflags libarchive)
LDFLAGS_ARCHIVE= $(shell pkg-config --libs libarchive)

download: main.c
	gcc -O2 -Wall $(CFLAGS_CURL) $(CFLAGS_ARCHIVE) main.c $(LDFLAGS_CURL) $(LDFLAGS_ARCHIVE) -o main -g -ggdb
