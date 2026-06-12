PROJECT := AMusicBrainz

AMIGA_PREFIX ?= /opt/amiga
NETINCLUDE_DIR ?= /opt/amiga-netinclude

CROSS := $(AMIGA_PREFIX)/bin/m68k-amigaos-
CC := $(CROSS)gcc

CPPFLAGS := -Iinclude -I$(NETINCLUDE_DIR)/include
CFLAGS := -O2 -Wall -Wextra -mcrt=nix13 -DAMITCP13_OS13

SRCS = src/amusicbrainz.c
OBJS = $(SRCS:.c=.o)

all: build/AMusicBrainz

build:
	mkdir -p build

build/AMusicBrainz: build $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(OBJS) build/AMusicBrainz

.PHONY: all clean
