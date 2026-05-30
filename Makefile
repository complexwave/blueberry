.PHONY: parser bytecode encoder decoder blueberry ci_timer_test ci_timer_sim ci_number_test ci_printf_test bytecode-dbg asan clean

.DEFAULT_GOAL := blueberry

CC = clang-23
CFLAGS = -Wall -O3 -march=native -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-missing-braces -fomit-frame-pointer -fzero-call-used-regs=skip  -Wno-unused-function -std=c11 -g -DCI_LEXER -DCI_DEBUG_NOFREE
#-DBB_VM_DEBUG

parser:
	$(CC) $(CFLAGS) -DSTANDALONE_PARSER -o parser parser.c cma/cma.c

bytecode:
	$(CC) $(CFLAGS) -o bytecode bytecode.c cma/cma.c

encoder:
	$(CC) $(CFLAGS) -o encoder encoder.c cma/cma.c

decoder:
	$(CC) $(CFLAGS) -o decoder decoder.c cma/cma.c

blueberry:
	$(CC) $(CFLAGS) -o blueberry blueberry.c cma/cma.c -lm

ci_timer_test:
	$(CC) $(CFLAGS) -DCI_TIMER_TEST -DCI_AUTONOW -o ci_timer_test ci_timer_test.c

ci_timer_sim:
	$(CC) $(CFLAGS) -DCI_TIMER_TEST -O2 -o ci_timer_sim ci_timer_sim.c

ci_number_test:
	$(CC) $(CFLAGS) -o ci_number_test ci_number_test.c -lm

ci_printf_test:
	$(CC) $(CFLAGS) -o ci_printf_test_simple ci_printf_test_simple.c

bytecode-dbg:
	$(CC) $(CFLAGS) -DB_DEBUG -o bytecode bytecode.c cma/cma.c

tracking:
	$(CC) $(CFLAGS) -DTGMEMLIB_TRACKING -o blueberry blueberry.c cma/cma.c -lm

ASAN_FLAGS = -O0 -g3 -fno-omit-frame-pointer -fno-inline -fno-pie -no-pie -fsanitize=address -DTGMEMLIB_TRACKING -DCI_LEXER -DCI_DEBUG_NOFREE -DCI_ASAN_TRACER -std=c11 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-missing-braces -Wno-unused-function

LSAN_SUPPRESS = LSAN_OPTIONS=suppressions=asan_suppress.txt

asan:
	$(CC) $(ASAN_FLAGS) -o blueberry blueberry.c cma/cma.c -lm
	$(CC) $(ASAN_FLAGS) -o bytecode bytecode.c cma/cma.c

asan-run:
	$(LSAN_SUPPRESS) ./blueberry $(ARGS)

clean:
	rm -f parser bytecode encoder decoder blueberry ci_timer_test ci_timer_sim
