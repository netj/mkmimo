# Makefile for mkmimo

PATH := $(shell pwd):$(PATH)
export PATH

test:
	cd test && ! type bats >/dev/null || bats *.bats
.PHONY: test
