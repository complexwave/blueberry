CC      = gcc
CFLAGS  = -O2 -g -Wall -Wextra -I..

TESTS   = test_basic test_arena_chain test_pattern test_stress test_edge test_ptr_arena test_heavy

.PHONY: all test clean

all: test

$(TESTS): %: %.c ../tgmemlib.c ../tgmemlib.h
	$(CC) $(CFLAGS) -o $@ $<

test: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t || exit 1; done
	@echo "=== ALL TESTS PASSED ==="

clean:
	rm -f $(TESTS)
