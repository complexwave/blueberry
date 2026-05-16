CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11

test: uscale_test
	./uscale_test

print: uscale_print
	./uscale_print

uscale_test: uscale_test.c uscale.c uscale_pow10.h
	$(CC) $(CFLAGS) -o $@ uscale_test.c -lm

uscale_print: uscale_print.c uscale.c uscale_pow10.h
	$(CC) $(CFLAGS) -o $@ uscale_print.c -lm

clean:
	rm -f uscale_test uscale_print

.PHONY: test print clean
