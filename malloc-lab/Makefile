#
# Students' Makefile for the Malloc Lab
#
TEAM = bovik
VERSION = 1
HANDINDIR = /afs/cs.cmu.edu/academic/class/15213-f01/malloclab/handin

CC = gcc
CFLAGS = -Wall -O2

# Every object file and every driver binary is written under build/, which
# git ignores, so the source directory stays clean.
BUILD = build

COMMON_OBJS = $(BUILD)/memlib.o $(BUILD)/fsecs.o $(BUILD)/fcyc.o $(BUILD)/clock.o $(BUILD)/ftimer.o

# Run a single trace instead of the whole default set:
#   make test-textbook TRACE=traces/realloc-bal.rep
TRACE =

all: $(BUILD)/mdriver

$(BUILD)/mdriver: $(BUILD)/mdriver.o $(BUILD)/mm.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/mdriver-naive: $(BUILD)/mdriver-naive.o $(BUILD)/mm-naive.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/mdriver-textbook: $(BUILD)/mdriver-textbook.o $(BUILD)/mm-textbook.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# mdriver.c is compiled once per allocator so that the results banner can
# name the allocator that is being tested.
$(BUILD)/mdriver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ mdriver.c

$(BUILD)/mdriver-naive.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h | $(BUILD)
	$(CC) $(CFLAGS) -DMM_NAME='"mm-naive"' -c -o $@ mdriver.c

$(BUILD)/mdriver-textbook.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h | $(BUILD)
	$(CC) $(CFLAGS) -DMM_NAME='"mm-textbook"' -c -o $@ mdriver.c

# Everything else is a one-source-file object.
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/mm.o $(BUILD)/mm-naive.o $(BUILD)/mm-textbook.o: mm.h memlib.h
$(BUILD)/fsecs.o $(BUILD)/ftimer.o: config.h
$(BUILD)/mdriver.o: mm.h memlib.h

$(BUILD):
	mkdir -p $(BUILD)

# Each test target builds and runs one allocator over the default trace set
# (config.h DEFAULT_TRACEFILES).  Pass TRACE=... to run a single trace.
test: $(BUILD)/mdriver
	$(BUILD)/mdriver -v $(if $(TRACE),-f $(TRACE),)

test-naive: $(BUILD)/mdriver-naive
	$(BUILD)/mdriver-naive -v $(if $(TRACE),-f $(TRACE),)

test-textbook: $(BUILD)/mdriver-textbook
	$(BUILD)/mdriver-textbook -v $(if $(TRACE),-f $(TRACE),)

handin:
	cp mm.c $(HANDINDIR)/$(TEAM)-$(VERSION)-mm.c

clean:
	rm -rf $(BUILD)

.PHONY: all test test-naive test-textbook handin clean
