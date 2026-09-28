CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LIBS    := -lncurses -lsqlite3
PREFIX  ?= /usr/local

.PHONY: all clean install

all: cdb

cdb: cDB.c
	$(CC) $(CFLAGS) -o $@ cDB.c $(LIBS)

install: cdb
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp cdb $(DESTDIR)$(PREFIX)/bin/cdb
	chmod 755 $(DESTDIR)$(PREFIX)/bin/cdb

clean:
	rm -f cdb
