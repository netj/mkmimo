# Makefile for mkmimo

CFLAGS += -Wall

mkmimo: mkmimo.o
	$(CC) -o $@ $(LDFLAGS) $^

PATH := $(shell pwd):$(PATH)
export PATH

test: mkmimo
	cd test && ! type bats >/dev/null || bats *.bats
.PHONY: test
