# Makefile for mkmimo

# no warnings allowed
CFLAGS += -Wall -Werror
# uses C99 (See: https://en.wikipedia.org/wiki/C99)
CFLAGS += --std=c9x
ifdef DEBUG
    # turn on debug flags
    CFLAGS += -g -DDEBUG
else
    # optimize!
    CFLAGS += -O2
endif
# MKMIMO_IMPL=multithreaded needs pthread
LDLIBS += -lpthread

# headers, sources
PRGM = mkmimo
SRCS += buffer.c
SRCS += mkmimo_nonblocking.c
SRCS += queue.c
SRCS += mkmimo_multithreaded.c
SRCS += main.c
HDRS += $(wildcard *.h)

# generated files
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:=.c=.d)

ifeq ($(MKMIMO_IMPL),bash)
$(PRGM): $(PRGM).sh
else ifeq ($(MKMIMO_IMPL),nodejs)
$(PRGM): $(PRGM).coffee node_modules
	echo '#!/usr/bin/env node'      >$@
	node_modules/.bin/coffee -p $< >>$@
	chmod +x $@
node_modules: package.json
	npm install
else
# how to link the program
$(PRGM): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $^ $(LDLIBS)
# compiler generated dependency
# See: http://stackoverflow.com/a/16969086
-include $(DEPS)
CFLAGS += -MMD
endif

clean:
	rm -f $(PRGM) $(OBJS) $(DEPS)
.PHONY: clean

# test with BATS
PATH := $(shell pwd):$(PATH)
export PATH
include test/bats.mk
test-build: $(PRGM)

# auto code formatting
.PHONY: format
ifndef CLANG_FORMAT
ifneq ($(shell type clang-format-3.7 2>/dev/null),)
CLANG_FORMAT = clang-format-3.7
else
CLANG_FORMAT = clang-format
endif
endif
format:
	$(CLANG_FORMAT) -i $(HDRS) $(SRCS)
