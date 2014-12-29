CC ?= gcc

VERSION = 0.1
PACKAGE = flood-$(VERSION)

PREFIX ?= /usr/local

# CFLAGS = -std=gnu89 -O2 -g -Wall -pthread -rdynamic -Werror -Wextra -Isrc $(OPTFLAGS)
CFLAGS = -std=gnu89 -O2 -g -Wall -pthread -rdynamic -Wno-unused -Isrc $(OPTFLAGS)
LIBS = -ldl -lm -lcurl -lleveldb -lsnappy $(OPTLIBS)

CLEANFILES = core core.* *.core *.o *.out src/*.o

all: flood listener broadcaster

flood: src/flood.o
	$(CC) $(CFLAGS) -o $@ src/flood.o $(LIBS)

listener: src/listener.o
	$(CC) $(CFLAGS) -o $@ src/listener.o $(LIBS)	

broadcaster: src/broadcaster.o
	$(CC) $(CFLAGS) -o $@ src/broadcaster.o $(LIBS)	

clean:
	$(RM) -Rf flood listener broadcaster $(CLEANFILES)

install:
	install flood $(PREFIX)/bin

dist: all | $(PACKAGE)
	cp -R src LICENSE Makefile README.md $(PACKAGE)
	tar -czf $(PACKAGE).tar.gz $(PACKAGE)

$(PACKAGE):
	mkdir -p $(PACKAGE)

.PHONY: all clean install dist
