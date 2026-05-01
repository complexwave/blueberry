.PHONY: parser bytecode encoder decoder blueberry clean

CC = clang-23
CFLAGS = -Wall -O3 -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-missing-braces -fomit-frame-pointer -fzero-call-used-regs=skip  -Wno-unused-function -std=c11 -g -DCI_LEXER -DCI_DISABLE_REFCOUNTING
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
	$(CC) $(CFLAGS) -o blueberry blueberry.c cma/cma.c

clean:
	rm -f parser bytecode encoder decoder blueberry
