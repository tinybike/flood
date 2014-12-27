CC=gcc
CFLAGS=-std=c89 -O2 -g -Wall -Wextra -rdynamic -pthread -pedantic -Isrc $(OPTFLAGS)
LIBS=-ldl -lm -lcurl -lleveldb -lsnappy $(OPTLIBS)
CLEANFILES=core core.* *.core *.o *.out src/*.o

all: flood

flood: src/flood.o
	$(CC) $(CFLAGS) -o $@ src/flood.o $(LIBS)

clean:
	$(RM) -Rf flood $(CLEANFILES)

install:
	install flood /usr/local/bin

.PHONY: all clean install
