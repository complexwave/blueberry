CC      = gcc
CFLAGS  = -O2 -g -Wall -Wextra -I ../ -DCI_ARRAY_TEST

TESTS   = test_reverse \
          test_rmoffset \
          test_basic \
          test_slice \
          test_circular_wrap \
          test_realloc_wrap \
          test_inline \
          test_upgrade \
          test_push_pop_stress \
          test_shift_unshift_stress \
          test_deque_pattern \
          test_ensure_space \
          test_set_index \
          test_clear \
          test_destructor \
          test_mixed_operations \
          test_edge_cases

.PHONY: all test clean

all: test

$(TESTS): %: %.c ../ciobj.c ../ciobj.h ../ci_array.c ../ci_string.c ../tgmemlib/tgmemlib.h ../tgmemlib/tgmemlib.c
	$(CC) $(CFLAGS) -o $@ $<

test: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t || exit 1; done
	@echo "=== ALL CI_ARRAY TESTS PASSED ==="

clean:
	rm -f $(TESTS)
