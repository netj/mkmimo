# Makefile for mkmimo

CFLAGS += -Wall --std=c9x
ifdef DEBUG
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2
endif

mkmimo: mkmimo.o
	$(CC) -o $@ $(LDFLAGS) $^

PATH := $(shell pwd):$(PATH)
export PATH

include test/bats.mk

clean:
	rm -f mkmimo *.o
.PHONY: clean
