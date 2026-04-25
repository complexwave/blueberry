// json_opus.c — CMA library usability test via JSON parsing
//
// Goal: test CMA idioms, discover friction points
// NOT a production JSON parser (no unicode, minimal escape handling)
//
// Structure:
//   1. Minimal JSON value type
//   2. Grammar (CMA patterns)
//   3. Capture walker (flat capture array → value tree)
//   4. Main
//
// FRICTION NOTES discovered during implementation:
//
// [F1] RESOLVED: notCHRS('"', '\\') added to lib.
//
// [F2] No sep_by primitive. Comma-separated lists require:
//        A(item, MIN(0, A(WS(), S(","), WS(), item)))
//      A SEP(item, S(",")) macro would reduce noise.
//
// [F3] WS() everywhere. The WS() macro helps vs manual R(" \t\n\r")
//      but JSON grammar is still noisy. A "lexeme mode" or auto-ws
//      combinator wrapping would clean this up.
//
// [F4] MAYBE uses non-static storage — only macro that does.
//      Using MAX(1, ...) as workaround (static, equivalent).
//
// [F5] RESOLVED: state.farthest tracks high-water mark of misses.
//
// [F6] Capture tag dispatch via single-char name works but feels
//      hacky. An enum/int tag on captures would be cleaner than
//      string comparison. Could use opctx for this.
//
// GOOD THINGS:
//
// [G1] Grammar reads well — the S/A/O/MIN/MAX/CAP vocabulary is
//      expressive enough to define JSON without hacks.
//
// [G2] REF(&ptr) for recursive grammars is simple and works.
//
// [G3] Capture nesting model (flat array + nested count) enables
//      clean recursive walk without building a capture tree.
//
// [G4] CAPn with short tags + walker pattern = clean separation
//      between grammar and semantic processing.
//
// [G5] CHRS() for explicit char codes is much clearer than R()
//      for non-range sets (escape chars, whitespace).
//

#include "cma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

// ================================================================
//  Minimal JSON value type
// ================================================================

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;
typedef struct jval jval;
typedef struct { char *key; jval *val; } jpair;

struct jval {
	jtype type;
	union {
		int    boolean;
		double number;
		struct { char *s; size_t len; } str;
		struct { jval **items; uint32_t count, cap; } arr;
		struct { jpair *pairs; uint32_t count, cap; } obj;
	};
};

static jval *jv_null(void)    { return calloc(1, sizeof(jval)); }
static jval *jv_bool(int b)   { jval *v = calloc(1, sizeof(*v)); v->type = J_BOOL; v->boolean = b; return v; }
static jval *jv_num(double n) { jval *v = calloc(1, sizeof(*v)); v->type = J_NUM;  v->number = n;  return v; }

static jval *jv_str(const char *s, size_t len) {
	jval *v = calloc(1, sizeof(*v));
	v->type = J_STR;
	v->str.s = malloc(len + 1);
	memcpy(v->str.s, s, len);
	v->str.s[len] = '\0';
	v->str.len = len;
	return v;
}

static jval *jv_arr(void) { jval *v = calloc(1, sizeof(*v)); v->type = J_ARR; return v; }
static jval *jv_obj(void) { jval *v = calloc(1, sizeof(*v)); v->type = J_OBJ; return v; }

static void jv_arr_push(jval *a, jval *item) {
	if (a->arr.count >= a->arr.cap) {
		a->arr.cap = a->arr.cap ? a->arr.cap * 2 : 4;
		a->arr.items = realloc(a->arr.items, a->arr.cap * sizeof(jval *));
	}
	a->arr.items[a->arr.count++] = item;
}

static void jv_obj_set(jval *o, const char *key, size_t klen, jval *val) {
	if (o->obj.count >= o->obj.cap) {
		o->obj.cap = o->obj.cap ? o->obj.cap * 2 : 4;
		o->obj.pairs = realloc(o->obj.pairs, o->obj.cap * sizeof(jpair));
	}
	jpair *p = &o->obj.pairs[o->obj.count++];
	p->key = malloc(klen + 1);
	memcpy(p->key, key, klen);
	p->key[klen] = '\0';
	p->val = val;
}

static void jv_free(jval *v) {
	if (!v) return;
	switch (v->type) {
	case J_STR: free(v->str.s); break;
	case J_ARR:
		for (uint32_t i = 0; i < v->arr.count; i++) jv_free(v->arr.items[i]);
		free(v->arr.items);
		break;
	case J_OBJ:
		for (uint32_t i = 0; i < v->obj.count; i++) {
			free(v->obj.pairs[i].key);
			jv_free(v->obj.pairs[i].val);
		}
		free(v->obj.pairs);
		break;
	default: break;
	}
	free(v);
}

// ================================================================
//  Grammar
// ================================================================
//
//  json    = ws value ws END
//  value   = string | number | "true" | "false" | "null" | array | object
//  string  = '"' (escape | strchar)* '"'
//  escape  = '\' ["\\/bfnrt]
//  strchar = <any byte except " and \>
//  number  = '-'? (0 | [1-9][0-9]*) ('.' [0-9]+)? ([eE][+-]?[0-9]+)?
//  array   = '[' ws (value (ws ',' ws value)*)? ws ']'
//  object  = '{' ws (member (ws ',' ws member)*)? ws '}'
//  member  = string ws ':' ws value
//

static cma_op *g_json_value = NULL;

static CMA_OP json_value_fn(cma_state *s, cma_op *op) {
	cma_op     *or_op = (cma_op *)op->opctx;
	cma_op_seq *seq   = (cma_op_seq *)or_op;
	size_t      at    = (size_t)(s->pos - s->str);

	if (cma_call(s, or_op))
		return cma_ret_matched(s);

	size_t reached = (size_t)(s->farthest - s->str);
	fprintf(stderr, "parse error at byte %zu (reached %zu): expected any of:", at, reached);
	for (size_t i = 0; i < seq->seq_len; i++) {
		fprintf(stderr, "%s %s", i ? "," : "", cma_op_pname(seq->seq[i]));
	}
	fprintf(stderr, "\n");

	return cma_ret_missed(s);
}

static cma_op *json_grammar(void) {
	// string
	cma_op *escape   = A(S("\\"), CHRS('"', '\\', '/', 'b', 'f', 'n', 'r', 't'));  // [G5]
	cma_op *str_char = O(escape, notCHRS('"', '\\'));
	cma_op *string   = NAMED("string", S("\""), CAPn(MIN(0, str_char), "s"), S("\""));

	// number
	cma_op *digits  = MIN(1, R("0-9"));
	cma_op *integer = O(S("0"), A(R("1-9"), MIN(0, R("0-9"))));
	cma_op *number  = NAMED("number", CAPn(A(
		MAX(1, S("-")),
		integer,
		MAX(1, A(S("."), digits)),                           // frac
		MAX(1, A(R("eE"), MAX(1, R("+-")), digits))         // exp
	), "n"));

	// literals
	cma_op *jtrue  = NAMED("true",  CAPn(S("true"),  "t") );
	cma_op *jfalse = NAMED("false", CAPn(S("false"), "f") );
	cma_op *jnull  = NAMED("null",  CAPn(S("null"),  "z") );

	// recursive reference — resolved at match time
	cma_op *vref = REF(&g_json_value);  // [G2]

	// array: [ value, value, ... ]
	cma_op *elems = A(WS(), vref, MIN(0, A(WS(), S(","), WS(), vref)));  // [F2]: want SEP(vref, S(","))
	cma_op *array = NAMED("array", CAPn(A(S("["), MAX(1, elems), WS(), S("]")), "a"));

	// object: { "key": value, ... }
	cma_op *member  = A(WS(), string, WS(), S(":"), WS(), vref);  // [F3]: ws everywhere
	cma_op *members = A(member, MIN(0, A(WS(), S(","), member)));
	cma_op *object  = NAMED("object", CAPn(A(S("{"), MAX(1, members), WS(), S("}")), "o") );

	g_json_value = EXEC(json_value_fn, O(string, number, jtrue, jfalse, jnull, array, object));
	
	
	
	return A(WS(), g_json_value, WS(), ENDL());
}

// ================================================================
//  Capture walker
// ================================================================
//
//  Captures form a flat array. Each capture has a nested count
//  indicating how many sub-captures it contains. Walk recursively:
//
//  Input:  {"a": [1, 2]}
//  Caps:   [0] "o" nested=4   — object
//          [1] "s" nested=0   — key "a"
//          [2] "a" nested=2   — array
//          [3] "n" nested=0   — number 1
//          [4] "n" nested=0   — number 2
//
//  The walker consumes captures in order. Container captures
//  (arrays, objects) consume their nested children via recursive
//  walk() calls. The nested count tells when to stop.
//

typedef struct { cma_state *s; uint32_t idx; } walker;

static jval *walk(walker *w);

static jval *walk_array(walker *w, uint32_t nested) {
	jval *arr = jv_arr();
	uint32_t consumed = 0;
	while (consumed < nested) {
		uint32_t before = w->idx;
		jv_arr_push(arr, walk(w));
		consumed += w->idx - before;
	}
	return arr;
}

static jval *walk_object(walker *w, uint32_t nested) {
	jval *obj = jv_obj();
	uint32_t consumed = 0;
	while (consumed < nested) {
		uint32_t before = w->idx;
		// key: always a "s" capture
		cma_capture *kc = cma_cap_get(w->s, w->idx++);
		consumed += w->idx - before;

		// value
		before = w->idx;
		jval *val = walk(w);
		consumed += w->idx - before;

		jv_obj_set(obj, (const char *)kc->start, cma_cap_len(kc), val);
	}
	return obj;
}

static jval *walk(walker *w) {
	cma_capture *c = cma_cap_get(w->s, w->idx++);
	char *tag = cma_cap_name(c);  // [F6]: single-char tag dispatch

	switch (tag[0]) {
	case 's': return jv_str((const char *)c->start, cma_cap_len(c));
	case 'n': {
		char buf[64];
		size_t len = cma_cap_len(c);
		if (len >= sizeof(buf)) len = sizeof(buf) - 1;
		memcpy(buf, c->start, len);
		buf[len] = '\0';
		return jv_num(strtod(buf, NULL));
	}
	case 't': return jv_bool(1);
	case 'f': return jv_bool(0);
	case 'z': return jv_null();
	case 'a': return walk_array(w, c->nested);
	case 'o': return walk_object(w, c->nested);
	default:
		fprintf(stderr, "unknown capture tag: %s\n", tag);
		return jv_null();
	}
}

// ================================================================
//  Print
// ================================================================

static void jprint(jval *v, int depth) {
	switch (v->type) {
	case J_NULL: printf("null"); break;
	case J_BOOL: printf("%s", v->boolean ? "true" : "false"); break;
	case J_NUM:  printf("%g", v->number); break;
	case J_STR:  printf("\"%s\"", v->str.s); break;
	case J_ARR:
		printf("[\n");
		for (uint32_t i = 0; i < v->arr.count; i++) {
			printf("%*s", (depth + 1) * 2, "");
			jprint(v->arr.items[i], depth + 1);
			if (i + 1 < v->arr.count) printf(",");
			printf("\n");
		}
		printf("%*s]", depth * 2, "");
		break;
	case J_OBJ:
		printf("{\n");
		for (uint32_t i = 0; i < v->obj.count; i++) {
			printf("%*s\"%s\": ", (depth + 1) * 2, "", v->obj.pairs[i].key);
			jprint(v->obj.pairs[i].val, depth + 1);
			if (i + 1 < v->obj.count) printf(",");
			printf("\n");
		}
		printf("%*s}", depth * 2, "");
		break;
	}
}

// ================================================================
//  File I/O + Main
// ================================================================

static char *read_file(const char *path, size_t *len) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror(path); return NULL; }
	struct stat st;
	fstat(fd, &st);
	char *buf = malloc(st.st_size + 1);
	*len = read(fd, buf, st.st_size);
	close(fd);
	buf[*len] = '\0';
	return buf;
}

static double now_sec(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void run_benchmark(int argc, char **argv, cma_op *grammar) {
	// preload all files
	int nfiles = argc;
	char **bufs = calloc(nfiles, sizeof(char *));
	size_t *lens = calloc(nfiles, sizeof(size_t));
	for (int i = 0; i < nfiles; i++) {
		bufs[i] = read_file(argv[i], &lens[i]);
		if (!bufs[i]) { fprintf(stderr, "failed to read %s\n", argv[i]); exit(1); }
	}

	double min_bench_time = 2.0;

	printf("%-30s %10s %10s %12s %10s\n", "file", "size", "iters", "decodes/s", "MB/s");
	printf("%-30s %10s %10s %12s %10s\n", "----", "----", "-----", "---------", "----");

	for (int i = 0; i < nfiles; i++) {
		// warmup + calibrate iterations
		int iters = 1;
		double elapsed = 0;

		// reuse state across iterations — avoid malloc/free per parse
		cma_state s;
		cma_init(&s, bufs[i], lens[i]);

		// first pass: find how many iters fill ~2s
		while (1) {
			double t0 = now_sec();
			for (int j = 0; j < iters; j++) {
				// reset state without reallocating
				s.pos       = s.str;
				s.farthest  = s.str;
				s.stack_len = 0;
				s.cap_len   = 0;
				s.status    = CMA_STATUS_OK;
				s.flags     = CMA_END_IS_MISS;

				int r = cma_match(&s, grammar);
				if (r < 0) { fprintf(stderr, "parse failed: %s\n", argv[i]); exit(1); }
			}
			elapsed = now_sec() - t0;
			if (elapsed >= min_bench_time) break;
			iters = (elapsed > 0.001) ? (int)(iters * min_bench_time / elapsed * 1.2) : iters * 10;
		}

		cma_free(&s);

		double dps = iters / elapsed;
		double mbps = (lens[i] * iters) / elapsed / (1024.0 * 1024.0);
		printf("%-30s %10zu %10d %12.1f %10.2f\n", argv[i], lens[i], iters, dps, mbps);
	}

	for (int i = 0; i < nfiles; i++) free(bufs[i]);
	free(bufs);
	free(lens);
}

int main(int argc, char **argv) {
	int bench = 0;
	int opt;
	while ((opt = getopt(argc, argv, "b")) != -1) {
		switch (opt) {
		case 'b': bench = 1; break;
		default:
			fprintf(stderr, "usage: %s [-b] <json-file>...\n", argv[0]);
			return 1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "usage: %s [-b] <json-file>...\n", argv[0]);
		return 1;
	}

	cma_op *grammar = json_grammar();

	if (bench) {
		run_benchmark(argc - optind, argv + optind, grammar);
		return 0;
	}

	for (int i = optind; i < argc; i++) {
		size_t len;
		char *buf = read_file(argv[i], &len);
		if (!buf) continue;

		printf("=== %s (%zu bytes) ===\n", argv[i], len);

		cma_state s;
		cma_init(&s, buf, len);
		int result = cma_match(&s, grammar);

		if (result < 0) {
			printf("  PARSE FAILED at byte %zu\n", (size_t)(s.farthest - s.str));
		} else {
			printf("  matched %d bytes, %u captures\n", result, cma_cap_count(&s));
			walker w = { .s = &s, .idx = 0 };
			jval *root = walk(&w);
			jprint(root, 0);
			printf("\n");
			jv_free(root);
		}

		cma_free(&s);
		free(buf);
		printf("\n");
	}
}
