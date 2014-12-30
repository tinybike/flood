CC ?= gcc

VERSION = 0.1
PACKAGE = flood-$(VERSION)

PREFIX ?= /usr/local

# CFLAGS = -std=gnu89 -O2 -g -Wall -Wno-unused -pthread -rdynamic -Werror -Wextra -Isrc $(OPTFLAGS)
CFLAGS = -std=gnu89 -O2 -g -Wno-unused -pthread -rdynamic -Isrc $(OPTFLAGS)
LIBS = -lm -ldl -lcurl -lleveldb -lsnappy $(OPTLIBS)

CLEANFILES = core core.* *.core *.o *.out *.a src/*.o

all: flood

flood: src/flood.o
	$(CC) $(CFLAGS) -o $@ src/flood.o $(LIBS)

listener: src/listener.o
	$(CC) $(CFLAGS) -o $@ src/listener.o $(LIBS)	

broadcaster: src/broadcaster.o
	$(CC) $(CFLAGS) -o $@ src/broadcaster.o $(LIBS)	

libtorrent:
	@$(MAKE) -C src/lt

clean:
	$(RM) -f flood $(CLEANFILES)

install:
	install flood $(PREFIX)/bin

dist: all | $(PACKAGE)
	cp -R src LICENSE Makefile README.md $(PACKAGE)
	tar -czf $(PACKAGE).tar.gz $(PACKAGE)

$(PACKAGE):
	mkdir -p $(PACKAGE)

.PHONY: all clean install dist
