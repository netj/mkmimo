# Makefile for mkmimo

CFLAGS += -Wall --std=c9x
ifdef DEBUG
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2
endif

mkmimo: main.o buffer.o mkmimo_nonblocking.o mkmimo_multithreaded.o queue.o
	$(CC) -o $@ $(LDFLAGS) $^

PATH := $(shell pwd):$(PATH)
export PATH

include test/bats.mk
test-build: mkmimo

clean:
	rm -f mkmimo *.o
.PHONY: clean

.PHONY: format
CLANG_FORMAT = clang-format
format:
	$(CLANG_FORMAT) -i *.[ch]
