CC      = gcc
CFLAGS  = -O2 -g -Wall -Wextra -I ../ -DCI_MAP_TEST

TESTS   = test_basic test_identity

.PHONY: all test clean

all: test

DEPS = ../ciobj.c ../ciobj.h ../ci_map.c ../ci_array.c ../ci_string.c \
       ../tgmemlib/tgmemlib.h ../tgmemlib/tgmemlib.c

$(TESTS): %: %.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ $<

test: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t || exit 1; done
	@echo "=== ALL CI_MAP TESTS PASSED ==="

clean:
	rm -f $(TESTS)
