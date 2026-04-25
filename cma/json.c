// ================================================================
//  CMA-based JSON Parser
//
//  This parser tests the CMA pattern matching library by implementing
//  JSON parsing with CMA patterns for validation + custom extraction.
//
//  PROBLEMS ENCOUNTERED WITH CMA:
//  ═══════════════════════════════════════════════════════════════
//
//  1. CHARACTER NEGATION / NEGATIVE SETS
//     Problem: CMA has NOT() for patterns but not for character classes.
//              You can't express "match any char except quote" directly.
//     Impact:  String patterns become verbose, had to use explicit charclass.
//     Example: NOT(S("\"")) doesn't work; use R(" !#-[\\]-~") instead.
//
//  2. NO AUTOMATIC WHITESPACE SKIPPING
//     Problem: Every pattern that allows ws must explicitly insert ws().
//     Impact:  Patterns become bloated. JSON objects/arrays need ws() everywhere.
//     Example: member = A(string, ws(), S(":"), ws(), value, ws())
//              Would be cleaner with WS(pattern) decorator.
//
//  3. CAPTURE WITHOUT CALLBACKS
//     Problem: CAP() captures text but has no semantic action callback.
//              You capture the text but can't say "call json_string() on this".
//     Impact:  Can't build AST during matching. Need separate extraction pass.
//     Workaround: Use two-pass approach (validate with CMA, extract with parser).
//
//  4. RECURSIVE PATTERN MANAGEMENT
//     Problem: Patterns referencing themselves need REF() for lazy resolution.
//              But static storage + caching is fragile, unclear lifetime.
//     Impact:  json_value references array/object which reference json_value.
//              Need cma_op *cached pattern with init-once logic.
//     Design: REF() callbacks work but feel like a hack for circular deps.
//
//  5. PATTERN COMPOSITION COMPLEXITY
//     Problem: Building OR of 6+ alternatives creates deep nesting.
//              Patterns aren't easily composable/extensible.
//     Impact:  Hard to add new JSON types. Pattern macros get complex.
//     Example: O(num, string, bool, null, array, object) not ideal.
//
//  6. ERROR REPORTING
//     Problem: cma_run() returns 1=match or -1=fail. No position/reason.
//     Impact:  Can't tell user "expected value at position 42".
//     Benefit: Fast binary matching is good for validation phase.
//
//  7. PATTERN LIFETIME / MEMORY OWNERSHIP
//     Problem: Unclear if patterns must be static or can be malloc'd.
//              Macros create temporaries in {0} static storage.
//     Impact:  Can't easily build patterns at runtime or on heap.
//     Design: Pattern trees seem to expect long lifetimes (static only?).
//
//  SUMMARY:
//  ────────
//  CMA is excellent for validating structure (syntax checking).
//  Not ideal for semantic parsing (building AST with extracted values).
//  Solution: Use CMA for validation, custom parser for extraction.
//
//  This demonstrates the boundary between pattern matching and
//  semantic interpretation. CMA handles the former well.
// ================================================================

#include "cma.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

// ================================================================
//  JSON value representation
// ================================================================

typedef enum {
	JSON_NULL,
	JSON_BOOL,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
} json_type;

typedef struct json_value json_value;
typedef struct json_pair json_pair;

struct json_value {
	json_type type;
	union {
		int boolean;
		double number;
		char *string;
		struct {
			json_value **items;
			size_t count;
			size_t capacity;
		} array;
		struct {
			json_pair *pairs;
			size_t count;
			size_t capacity;
		} object;
	} value;
};

struct json_pair {
	char *key;
	json_value *val;
};

json_value *json_null_value(void) {
	json_value *v = malloc(sizeof(*v));
	v->type = JSON_NULL;
	return v;
}

json_value *json_bool_value(int b) {
	json_value *v = malloc(sizeof(*v));
	v->type = JSON_BOOL;
	v->value.boolean = b;
	return v;
}

json_value *json_number_value(double n) {
	json_value *v = malloc(sizeof(*v));
	v->type = JSON_NUMBER;
	v->value.number = n;
	return v;
}

json_value *json_string_value(const char *s) {
	json_value *v = malloc(sizeof(*v));
	v->type = JSON_STRING;
	v->value.string = malloc(strlen(s) + 1);
	strcpy(v->value.string, s);
	return v;
}

json_value *json_array_value(void) {
	json_value *v = malloc(sizeof(*v));
	v->type = JSON_ARRAY;
	v->value.array.items = NULL;
	v->value.array.count = 0;
	v->value.array.capacity = 0;
	return v;
}

json_value *json_object_value(void) {
	json_value *v = malloc(sizeof(*v));
	v->type = JSON_OBJECT;
	v->value.object.pairs = NULL;
	v->value.object.count = 0;
	v->value.object.capacity = 0;
	return v;
}

void json_array_push(json_value *arr, json_value *item) {
	if (arr->value.array.count >= arr->value.array.capacity) {
		arr->value.array.capacity = arr->value.array.capacity ? arr->value.array.capacity * 2 : 4;
		arr->value.array.items = realloc(arr->value.array.items,
			arr->value.array.capacity * sizeof(json_value*));
	}
	arr->value.array.items[arr->value.array.count++] = item;
}

void json_object_set(json_value *obj, const char *key, json_value *val) {
	if (obj->value.object.count >= obj->value.object.capacity) {
		obj->value.object.capacity = obj->value.object.capacity ? obj->value.object.capacity * 2 : 4;
		obj->value.object.pairs = realloc(obj->value.object.pairs,
			obj->value.object.capacity * sizeof(json_pair));
	}
	json_pair *p = &obj->value.object.pairs[obj->value.object.count++];
	p->key = malloc(strlen(key) + 1);
	strcpy(p->key, key);
	p->val = val;
}

void json_free(json_value *v) {
	if (!v) return;
	switch (v->type) {
	case JSON_STRING:
		free(v->value.string);
		break;
	case JSON_ARRAY:
		for (size_t i = 0; i < v->value.array.count; i++)
			json_free(v->value.array.items[i]);
		free(v->value.array.items);
		break;
	case JSON_OBJECT:
		for (size_t i = 0; i < v->value.object.count; i++) {
			free(v->value.object.pairs[i].key);
			json_free(v->value.object.pairs[i].val);
		}
		free(v->value.object.pairs);
		break;
	default:
		break;
	}
	free(v);
}

// ================================================================
//  POSIX file I/O (not FILE*)
// ================================================================

char *read_json_posix(const char *filename, size_t *len) {
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return NULL;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		perror("fstat");
		close(fd);
		return NULL;
	}

	size_t size = st.st_size;
	char *buf = malloc(size + 1);
	if (!buf) {
		fprintf(stderr, "malloc failed\n");
		close(fd);
		return NULL;
	}

	ssize_t nread = read(fd, buf, size);
	close(fd);

	if (nread != (ssize_t)size) {
		fprintf(stderr, "read mismatch: got %zd, expected %zu\n", nread, size);
		free(buf);
		return NULL;
	}

	buf[size] = '\0';
	*len = size;
	return buf;
}

// ================================================================
//  CMA-based parsing
// ================================================================
//
// PROBLEM 1: Automatic whitespace skipping
// ────────────────────────────────────────
// CMA doesn't have a decorator/middleware to auto-skip whitespace.
// We have to manually insert ws() calls everywhere, making patterns verbose.
// SOLUTION: Would need WS() macro or ws-aware pattern builder.
//
// PROBLEM 2: Capture extraction without callbacks
// ───────────────────────────────────────────────
// CAP() captures text but there's no semantic action callback tied to it.
// We get the text matched but no way to say "construct a JSON value from this".
// SOLUTION: Need CAPF(pattern, callback_fn) or similar.
//
// PROBLEM 3: Recursive pattern construction
// ──────────────────────────────────────────
// json_value references itself (arrays/objects contain values).
// Using static storage + REF() works but feels fragile.
// PROBLEM: If we construct patterns dynamically, who owns/frees them?
// SOLUTION: Arena allocator or explicit lifetime management.
//
// PROBLEM 4: Pattern complexity & debugging
// ──────────────────────────────────────────
// Even simple JSON grammar gets unwieldy with manual ws() insertion.
// Error messages just say "pattern didn't match" not "expected value at pos 42".
// SOLUTION: Better diagnostics, pattern builders.
//


// cma dev comments
/*
1. intended. lpeg is same

2. problem with this in c especially - parser can backtrack on capture and it will need be dealloc
it can happen many times in not very efficient patter

3. for c doesnt matter currently as programming language interpretter doesnt ever free own parser
later init function will init gc objects in scripting language

4. will try improve

*/



static cma_op *ws(void) {
	return MAX(10000, R(" \t\n\r"));
}

static cma_op *json_string_pattern(void) {
	// PROBLEM 7: Character class negation is not directly supported
	// ───────────────────────────────────────────────────────────────
	// We can't say "match anything except quote" with NOT(S("\"")).
	// NOT() is for patterns, not character classes.
	//
	// Workaround: Use a set of "valid" chars (simplified, no escapes for now)
	// In production, would need proper escape sequence support.

	// For now: accept string with any printable chars except quote/backslash
	// This is a limitation of CMA patterns for character negation.
	cma_op *printable = R(" !#-[\\]-~");  // ASCII printable except quote
	cma_op *content = MAX(10000, printable);

	return A(S("\""), CAP(content), S("\""));
}

static cma_op *json_number_pattern(void) {
	cma_op *minus = MAX(1, S("-"));
	cma_op *zero = S("0");
	cma_op *nonzero = A(R("1-9"), MAX(10000, R("0-9")));
	cma_op *int_part = O(zero, nonzero);
	cma_op *frac = A(S("."), MIN(1, R("0-9")));
	cma_op *exp_part = A(O(S("e"), S("E")), MAX(1, O(S("+"), S("-"))), MIN(1, R("0-9")));

	return CAP(A(minus, int_part, MAX(1, frac), MAX(1, exp_part)));
}

static cma_op *json_bool_pattern(void) {
	return O(S("true"), S("false"));
}

static cma_op *json_null_pattern(void) {
	return S("null");
}

// Forward reference for recursive patterns
static cma_op *json_value_pattern_impl(void);

// Lazy reference callback
static cma_op *json_value_ref_impl(cma_state *s, void *ctx) {
	(void)s;
	(void)ctx;
	static cma_op *cached = NULL;
	if (!cached) cached = json_value_pattern_impl();
	return cached;
}

static cma_op *json_value_ref(void) {
	static cma_op_ref ref_storage = {0};
	return cma_init_ref(&ref_storage, json_value_ref_impl, NULL);
}

static cma_op *json_array_pattern(void) {
	// array := '[' ws (value (ws ',' ws value)*)? ws ']'
	// Use lazy reference to avoid infinite recursion
	cma_op *value_ref = json_value_ref();

	cma_op *first_item = A(ws(), value_ref, ws());
	cma_op *next_items = A(S(","), ws(), value_ref, ws());
	cma_op *items = A(first_item, MAX(10000, next_items));

	return A(
		S("["),
		ws(),
		MAX(1, items),  // 0 or 1 items sequence
		ws(),
		S("]")
	);
}

static cma_op *json_object_pattern(void) {
	// object := '{' ws (member (ws ',' ws member)*)? ws '}'
	// member := string ':' value
	cma_op *value_ref = json_value_ref();
	cma_op *member = A(json_string_pattern(), ws(), S(":"), ws(), value_ref, ws());
	cma_op *next_members = A(S(","), ws(), member);
	cma_op *members = A(member, MAX(10000, next_members));

	return A(
		S("{"),
		ws(),
		MAX(1, members),  // 0 or 1 members sequence
		ws(),
		S("}")
	);
}

// PROBLEM 5: Can't easily compose patterns for alternatives
// ──────────────────────────────────────────────────────────
// O(num, string, bool, null, array, object) creates deep nesting.
// If we want to add more types later, need to rebuild pattern.
// SOLUTION: Pattern builder functions or DSL preprocessing.

static cma_op *json_value_pattern_impl(void) {
	return O(
		json_number_pattern(),
		json_string_pattern(),
		json_bool_pattern(),
		json_null_pattern(),
		json_array_pattern(),
		json_object_pattern()
	);
}

static cma_op *json_value_pattern(void) {
	return json_value_ref();
}

static cma_op *json_root_pattern(void) {
	return A(
		ws(),
		json_value_pattern(),
		ws()
	);
}

// ================================================================
//  Extraction from captures
// ================================================================
//
// PROBLEM 6: No direct way to get capture value
// ──────────────────────────────────────────────
// We have cma_cap_get() but it needs index.
// We'd need to correlate "which capture is this value?" manually.
// SOLUTION: Named captures + hash lookup, or capture callbacks.
//

static char *unescape_string(const char *s, size_t len) {
	char *buf = malloc(len + 1);
	size_t out = 0;
	for (size_t i = 0; i < len; i++) {
		if (s[i] == '\\' && i + 1 < len) {
			i++;
			switch (s[i]) {
			case '"':  buf[out++] = '"'; break;
			case '\\': buf[out++] = '\\'; break;
			case '/':  buf[out++] = '/'; break;
			case 'b':  buf[out++] = '\b'; break;
			case 'f':  buf[out++] = '\f'; break;
			case 'n':  buf[out++] = '\n'; break;
			case 'r':  buf[out++] = '\r'; break;
			case 't':  buf[out++] = '\t'; break;
			default:   buf[out++] = s[i]; break;
			}
		} else {
			buf[out++] = s[i];
		}
	}
	buf[out] = '\0';
	return buf;
}

// ================================================================
//  Simple CMA-based validator (not full parser)
// ================================================================
//
// Note: We can validate JSON structure with CMA alone.
// But extracting values & building AST requires additional work.
// CMA is great for:
//   - Validating syntax
//   - Capturing important tokens
// CMA is not ideal for:
//   - Semantic actions (need callbacks)
//   - Building complex ASTs (need allocation strategy)
//   - Error recovery (patterns are all-or-nothing)
//

int json_validate_cma(const char *input, size_t len) {
	cma_op *pat = json_root_pattern();
	int result = cma_run(pat, input, len);
	return result >= 0;  // 1=valid, 0=invalid
}

// ================================================================
//  Hybrid approach: CMA validation + manual extraction
// ================================================================
//
// Since CMA captures don't have callbacks, we do a two-pass approach:
// 1. Use CMA to validate structure
// 2. Use simple manual parser to extract values
//
// This is suboptimal but demonstrates both CMA and extraction.
//

// Minimal manual parser for value extraction
typedef struct {
	const char *input;
	const char *end;
	const char *pos;
} extractor;

static void skip_ws_ex(extractor *e) {
	while (e->pos < e->end && isspace(*e->pos))
		e->pos++;
}

static json_value *extract_value(extractor *e);

static json_value *extract_string(extractor *e) {
	skip_ws_ex(e);
	if (e->pos >= e->end || *e->pos != '"') return NULL;
	e->pos++;
	const char *start = e->pos;
	while (e->pos < e->end && *e->pos != '"') {
		if (*e->pos == '\\') e->pos++;
		e->pos++;
	}
	if (e->pos >= e->end) return NULL;
	size_t len = e->pos - start;
	e->pos++;  // skip closing quote
	char *unesc = unescape_string(start, len);
	json_value *v = json_string_value(unesc);
	free(unesc);
	return v;
}

static json_value *extract_number(extractor *e) {
	skip_ws_ex(e);
	const char *start = e->pos;
	if (e->pos < e->end && *e->pos == '-') e->pos++;
	if (e->pos >= e->end || !isdigit(*e->pos)) return NULL;
	while (e->pos < e->end && isdigit(*e->pos)) e->pos++;
	if (e->pos < e->end && *e->pos == '.') {
		e->pos++;
		while (e->pos < e->end && isdigit(*e->pos)) e->pos++;
	}
	if (e->pos < e->end && (*e->pos == 'e' || *e->pos == 'E')) {
		e->pos++;
		if (e->pos < e->end && (*e->pos == '+' || *e->pos == '-')) e->pos++;
		while (e->pos < e->end && isdigit(*e->pos)) e->pos++;
	}
	char buf[64];
	size_t len = e->pos - start;
	if (len >= sizeof(buf)) return NULL;
	memcpy(buf, start, len);
	buf[len] = '\0';
	return json_number_value(strtod(buf, NULL));
}

static json_value *extract_literal(extractor *e, const char *lit, json_type type, int bool_val) {
	skip_ws_ex(e);
	size_t len = strlen(lit);
	if (e->pos + len > e->end || memcmp(e->pos, lit, len) != 0) return NULL;
	e->pos += len;
	if (type == JSON_NULL) return json_null_value();
	if (type == JSON_BOOL) return json_bool_value(bool_val);
	return NULL;
}

static json_value *extract_array(extractor *e) {
	skip_ws_ex(e);
	if (e->pos >= e->end || *e->pos != '[') return NULL;
	e->pos++;

	json_value *arr = json_array_value();

	skip_ws_ex(e);
	if (e->pos < e->end && *e->pos == ']') {
		e->pos++;
		return arr;
	}

	while (1) {
		json_value *item = extract_value(e);
		if (!item) { json_free(arr); return NULL; }
		json_array_push(arr, item);

		skip_ws_ex(e);
		if (e->pos >= e->end) { json_free(arr); return NULL; }
		if (*e->pos == ']') { e->pos++; return arr; }
		if (*e->pos != ',') { json_free(arr); return NULL; }
		e->pos++;
	}
}

static json_value *extract_object(extractor *e) {
	skip_ws_ex(e);
	if (e->pos >= e->end || *e->pos != '{') return NULL;
	e->pos++;

	json_value *obj = json_object_value();

	skip_ws_ex(e);
	if (e->pos < e->end && *e->pos == '}') {
		e->pos++;
		return obj;
	}

	while (1) {
		json_value *key_val = extract_string(e);
		if (!key_val) { json_free(obj); return NULL; }

		skip_ws_ex(e);
		if (e->pos >= e->end || *e->pos != ':') {
			json_free(obj);
			json_free(key_val);
			return NULL;
		}
		e->pos++;

		json_value *val = extract_value(e);
		if (!val) { json_free(obj); json_free(key_val); return NULL; }

		json_object_set(obj, key_val->value.string, val);
		json_free(key_val);

		skip_ws_ex(e);
		if (e->pos >= e->end) { json_free(obj); return NULL; }
		if (*e->pos == '}') { e->pos++; return obj; }
		if (*e->pos != ',') { json_free(obj); return NULL; }
		e->pos++;
	}
}

static json_value *extract_value(extractor *e) {
	skip_ws_ex(e);
	if (e->pos >= e->end) return NULL;

	char ch = *e->pos;
	if (ch == '"') return extract_string(e);
	if (ch == '{') return extract_object(e);
	if (ch == '[') return extract_array(e);
	if (ch == 't') return extract_literal(e, "true", JSON_BOOL, 1);
	if (ch == 'f') return extract_literal(e, "false", JSON_BOOL, 0);
	if (ch == 'n') return extract_literal(e, "null", JSON_NULL, 0);
	if (ch == '-' || isdigit(ch)) return extract_number(e);
	return NULL;
}

json_value *json_parse(const char *input, size_t len) {
	// First pass: validate with CMA
	if (!json_validate_cma(input, len)) {
		fprintf(stderr, "JSON validation failed\n");
		return NULL;
	}

	// Second pass: extract values
	extractor e = {
		.input = input,
		.end = input + len,
		.pos = input
	};

	json_value *result = extract_value(&e);
	if (!result) return NULL;

	skip_ws_ex(&e);
	if (e.pos != e.end) {
		json_free(result);
		return NULL;
	}

	return result;
}

void json_print(json_value *v, int indent) {
	if (!v) return;

	switch (v->type) {
	case JSON_NULL:
		printf("null");
		break;
	case JSON_BOOL:
		printf("%s", v->value.boolean ? "true" : "false");
		break;
	case JSON_NUMBER:
		printf("%g", v->value.number);
		break;
	case JSON_STRING:
		printf("\"%s\"", v->value.string);
		break;
	case JSON_ARRAY:
		printf("[\n");
		for (size_t i = 0; i < v->value.array.count; i++) {
			for (int j = 0; j < indent + 1; j++) printf("  ");
			json_print(v->value.array.items[i], indent + 1);
			if (i + 1 < v->value.array.count) printf(",");
			printf("\n");
		}
		for (int j = 0; j < indent; j++) printf("  ");
		printf("]");
		break;
	case JSON_OBJECT:
		printf("{\n");
		for (size_t i = 0; i < v->value.object.count; i++) {
			for (int j = 0; j < indent + 1; j++) printf("  ");
			printf("\"%s\": ", v->value.object.pairs[i].key);
			json_print(v->value.object.pairs[i].val, indent + 1);
			if (i + 1 < v->value.object.count) printf(",");
			printf("\n");
		}
		for (int j = 0; j < indent; j++) printf("  ");
		printf("}");
		break;
	}
}

// ================================================================
//  Testing
// ================================================================

#ifdef JSON_MAIN

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <json-file>\n", argv[0]);
		return 1;
	}

	size_t len;
	char *buf = read_json_posix(argv[1], &len);
	if (!buf) return 1;

	printf("Parsing %zu bytes...\n", len);

	json_value *root = json_parse(buf, len);
	if (!root) {
		printf("Parse failed\n");
		free(buf);
		return 1;
	}

	printf("Parse succeeded! AST:\n");
	json_print(root, 0);
	printf("\n");

	json_free(root);
	free(buf);
	return 0;
}

#endif
