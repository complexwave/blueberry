/*
 * test_inc.c — ci_inc / ci_dec smoke test
 *
 * Creates one of each object type + non-refcountable values,
 * runs ci_inc then ci_dec on each, printing before/after to
 * find where the asm version crashes.
 *
 * Build:  make test_inc
 */

#define BB_NO_MAIN
#define BYTECODE_NO_MAIN
#include "blueberry.c"

#include <stdio.h>

typedef struct { const char *name; ci_ptr ptr; } subj_t;

static void show_refcnt(const char *label, const char *op, ci_ptr p) {
	if (CI_IS_REFCOUNTABLE(p)) {
		ci_gchdr *hdr = (ci_gchdr *)p;
		printf("  %-20s %-6s refcnt=%u\n", label, op, hdr->refcnt);
	} else {
		printf("  %-20s %-6s (not refcountable)\n", label, op);
	}
}

int main(void) {
	ci_init();
	ci_str_register();
	ci_arr_register();
	ci_map_register();
	bb_vm_types_register();

	ci_str     *str_big   = ci_str_from_cstr("hello world this is a full heap string");
	ci_ptr      str_small = ci_str_small_new("hi", 2);
	ci_array   *arr       = ci_arr_new(4);
	ci_map     *map       = ci_map_new(8);
	bb_vm      *vm        = ci_new(CI_BB_VM);
	bb_coro    *coro      = ci_new(CI_BB_CORO);
	bb_closure *closure   = ci_new(CI_BB_CLOSURE);

	subj_t subjects[] = {
		{ "null",             NULL              },
		{ "PACKINT(0)",       CI_PACKINT(0)     },
		{ "PACKINT(42)",      CI_PACKINT(42)    },
		{ "BOOL(true)",       CI_BOOL(1)        },
		{ "BOOL(false)",      CI_BOOL(0)        },
		{ "ci_str big",       str_big           },
		{ "ci_str small",     str_small         },
		{ "ci_array",         arr               },
		{ "ci_map",           map               },
		{ "bb_vm",            vm                },
		{ "bb_coro",          coro              },
		{ "bb_closure",       closure           },
	};
	int nsub = (int)(sizeof(subjects)/sizeof(subjects[0]));

	printf("=== ci_inc test ===\n");
	for (int i = 0; i < nsub; i++) {
		show_refcnt(subjects[i].name, "before", subjects[i].ptr);
		fflush(stdout);
		ci_inc(subjects[i].ptr);
		show_refcnt(subjects[i].name, "after", subjects[i].ptr);
		fflush(stdout);
	}

	printf("\n=== ci_dec test ===\n");
	for (int i = 0; i < nsub; i++) {
		show_refcnt(subjects[i].name, "before", subjects[i].ptr);
		fflush(stdout);
		ci_dec(subjects[i].ptr);
		show_refcnt(subjects[i].name, "after", subjects[i].ptr);
		fflush(stdout);
	}

	printf("\nall ok\n");
	ci_shutdown();
	return 0;
}
