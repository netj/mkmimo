# Makefile for mkmimo

CFLAGS += -Wall --std=c9x
ifdef DEBUG
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2
endif

ifneq ($(MKMIMO_IMPL),bash)
mkmimo: main.o buffer.o mkmimo_nonblocking.o mkmimo_multithreaded.o queue.o
	$(CC) -o $@ $(LDFLAGS) $^
else
mkmimo: mkmimo.sh
endif

PATH := $(shell pwd):$(PATH)
export PATH

include test/bats.mk
test-build: mkmimo

clean:
	rm -f mkmimo *.o
.PHONY: clean

.PHONY: format
ifndef CLANG_FORMAT
ifneq ($(shell type clang-format-3.7 2>/dev/null),)
CLANG_FORMAT = clang-format-3.7
else
CLANG_FORMAT = clang-format
endif
endif
format:
	$(CLANG_FORMAT) -i *.[ch]
