CC ?= gcc

VERSION = 0.1
PACKAGE = flood-$(VERSION)

PREFIX ?= /usr/local

CFLAGS = -std=c99 -O2 -g -Wall -pthread -rdynamic -pedantic -Wextra -Isrc $(OPTFLAGS)
LIBS = -ldl -lm -lcurl -lleveldb -lsnappy $(OPTLIBS)

CLEANFILES = core core.* *.core *.o *.out src/*.o

all: flood

flood: src/flood.o
	$(CC) $(CFLAGS) -o $@ src/flood.o $(LIBS)

clean:
	$(RM) -Rf flood $(CLEANFILES)

install:
	install flood $(PREFIX)/bin

dist: all | $(PACKAGE)
	cp -R src LICENSE Makefile README.md $(PACKAGE)
	tar -czf $(PACKAGE).tar.gz $(PACKAGE)

$(PACKAGE):
	mkdir -p $(PACKAGE)

.PHONY: all clean install dist
