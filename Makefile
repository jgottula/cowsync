# asx
# (c) 2012 Justin Gottula
# The source code of this project is distributed under the terms of the
# simplified BSD license. See the LICENSE file for details.


CC:=gcc
CFLAGS:=-std=gnu11 -Wall -Wextra -g -O2 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LIBS:=

# evaluated when used
SOURCES=$(shell find src -type f -iname '*.c')
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))
EXE=bin/cowsync
CLEAN=$(wildcard $(EXE)) $(wildcard src/*.o)


.PHONY: all clean

# default rule
all: $(EXE)

$(EXE): $(OBJECTS) Makefile
	$(CC) $(CFLAGS) $(LIBS) -o $@ $(OBJECTS)

src/%.o: src/%.c Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(CLEAN)
