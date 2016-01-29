# Makefile for mkmimo

CFLAGS += -Wall --std=c9x
ifdef DEBUG
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2
endif

ifdef MULTITHREADED
mkmimo: mkmimo_multithreaded.o queue.o
	$(CC) -o $@ $(LDFLAGS) $^
else
mkmimo: main.o utils.o mkmimo_nonblocking.o
	$(CC) -o $@ $(LDFLAGS) $^
endif

PATH := $(shell pwd):$(PATH)
export PATH

include test/bats.mk
test-build: mkmimo

clean:
	rm -f mkmimo *.o
.PHONY: clean
