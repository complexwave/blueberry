.PHONY: parser bytecode encoder decoder blueberry ci_timer_test ci_timer_sim ci_number_test clean

.DEFAULT_GOAL := blueberry

CC = clang-23
CFLAGS = -Wall -O3 -march=native -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-missing-braces -fomit-frame-pointer -fzero-call-used-regs=skip  -Wno-unused-function -std=c11 -g -DCI_LEXER -DCI_DISABLE_REFCOUNTING
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

clean:
	rm -f parser bytecode encoder decoder blueberry ci_timer_test ci_timer_sim
