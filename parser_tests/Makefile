PARSER  = ../parser
PFLAGS  = -Wall -Wextra -Wno-missing-braces -std=c11 -g -DCI_LEXER

TESTS   = $(wildcard *.ct)

.PHONY: all test clean build

all: test

build: $(PARSER)

$(PARSER): ../parser.c ../lexer.c ../ciobj.c ../ciobj.h ../ci_string.c ../ci_array.c \
           ../tgmemlib/tgmemlib.h ../tgmemlib/tgmemlib.c ../cma/cma.h ../cma/cma.c
	gcc $(PFLAGS) -o $@ ../parser.c ../cma/cma.c

test: build
	@fail=0; \
	for t in $(TESTS); do \
		name=$${t%.ct}; \
		expected="$$name.expected"; \
		if [ ! -f "$$expected" ]; then \
			echo "SKIP $$name (no .expected)"; \
			continue; \
		fi; \
		actual=$$($(PARSER) "$$t" 2>&1); \
		diff_out=$$(echo "$$actual" | diff -u "$$expected" - 2>&1); \
		if [ $$? -eq 0 ]; then \
			echo "PASS $$name"; \
		else \
			echo "FAIL $$name"; \
			echo "$$diff_out"; \
			fail=1; \
		fi; \
	done; \
	if [ $$fail -eq 1 ]; then echo "=== SOME TESTS FAILED ==="; exit 1; \
	else echo "=== ALL PARSER TESTS PASSED ==="; fi

# Generate/update .expected from current parser output
update: build
	@for t in $(TESTS); do \
		name=$${t%.ct}; \
		$(PARSER) "$$t" 2>&1 > "$$name.expected"; \
		echo "updated $$name.expected"; \
	done

clean:
	rm -f $(PARSER)
