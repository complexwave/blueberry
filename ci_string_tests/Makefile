CC      = gcc
CFLAGS  = -O2 -g -Wall -Wextra -I ../ -DCI_STRING_TEST

TESTS   = test_small_basic test_small_boundaries test_str_new test_str_from_cstr \
          test_str_copy test_accessors_poly test_ensure_tail test_put_tail \
          test_ensure_head test_compact test_append test_prepend \
          test_rmhead test_rmtail test_clear test_hash test_hash_invalidation \
          test_eq test_syscall_pattern test_producer_consumer \
          test_concat_big test_realloc_stress test_lifecycle_mixed \
          test_small_poly

.PHONY: all test clean

all: test

$(TESTS): %: %.c ../ciobj.c ../ciobj.h ../ci_string.c ../tgmemlib/tgmemlib.h ../tgmemlib/tgmemlib.c
	$(CC) $(CFLAGS) -o $@ $<

test: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t || exit 1; done
	@echo "=== ALL CI_STRING TESTS PASSED ==="

clean:
	rm -f $(TESTS)
