#include "cma.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

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
		int boolean;              // JSON_BOOL
		double number;            // JSON_NUMBER
		char *string;             // JSON_STRING
		struct {
			json_value **items;
			size_t count;
			size_t capacity;
		} array;                  // JSON_ARRAY
		struct {
			json_pair *pairs;
			size_t count;
			size_t capacity;
		} object;                 // JSON_OBJECT
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
//  Whitespace
// ================================================================

static cma_op *ws(void) {
	return MAX(10000, R(" \t\n\r"));
}

// ================================================================
//  Strings with escape sequences
// ================================================================
// string := '"' <string-chars>* '"'
// string-chars := (not quote/backslash) | escape-seq
// escape-seq := '\' ('"' | '\' | '/' | 'b' | 'f' | 'n' | 'r' | 't' | 'u' hex{4})

static cma_op *json_string_content(void) {
	// Match either non-special chars or valid escape sequences
	cma_op *regular = NOT(O(S("\""), S("\\")));
	cma_op *escape = A(S("\\"), O(S("\""), S("\\"), S("/"), S("b"), S("f"), S("n"), S("r"), S("t")));
	cma_op *hex4 = A(R("0-9a-fA-F"), R("0-9a-fA-F"), R("0-9a-fA-F"), R("0-9a-fA-F"));
	cma_op *escape_unicode = A(S("\\u"), hex4);

	return MAX(10000, O(regular, escape, escape_unicode));
}

static cma_op *json_string(void) {
	return A(
		S("\""),
		CAP(json_string_content()),
		S("\"")
	);
}

// ================================================================
//  Numbers
// ================================================================
// number := ['-'] ('0' | [1-9][0-9]*) ['.' [0-9]+] [([eE][+-]?[0-9]+)]

static cma_op *json_number(void) {
	cma_op *sign = MAX(1, S("-"));
	cma_op *zero = S("0");
	cma_op *nonzero = A(R("1-9"), MAX(10000, R("0-9")));
	cma_op *int_part = O(zero, nonzero);

	cma_op *frac = A(S("."), MIN(1, R("0-9")));
	cma_op *frac_opt = MAX(1, frac);

	cma_op *exp_sign = MAX(1, O(S("+"), S("-")));
	cma_op *exponent = A(O(S("e"), S("E")), exp_sign, MIN(1, R("0-9")));
	cma_op *exp_opt = MAX(1, exponent);

	return CAP(A(sign, int_part, frac_opt, exp_opt));
}

// ================================================================
//  Booleans and Null
// ================================================================

static cma_op *json_true(void) {
	return S("true");
}

static cma_op *json_false(void) {
	return S("false");
}

static cma_op *json_null(void) {
	return S("null");
}

static cma_op *json_bool(void) {
	return O(json_true(), json_false());
}

// ================================================================
//  Recursive patterns with REF
// ================================================================

// Forward declarations for lazy evaluation
static cma_op *json_value_ref(void);

static cma_op *json_array(void) {
	cma_op *value_ref = json_value_ref();

	// array := '[' ws (value (ws ',' ws value)*)? ws ']'
	cma_op *item = A(ws(), value_ref, ws());
	cma_op *separator = A(S(","), ws());
	cma_op *items = A(item, MAX(10000, A(separator, value_ref, ws())));

	return A(
		S("["),
		ws(),
		MAX(1, items),  // 0 or 1 occurrence of items
		ws(),
		S("]")
	);
}

static cma_op *json_object(void) {
	cma_op *value_ref = json_value_ref();

	// object := '{' ws (string ':' value (ws ',' ...)?)? ws '}'
	cma_op *member = A(json_string(), ws(), S(":"), ws(), value_ref, ws());
	cma_op *separator = A(S(","), ws());
	cma_op *members = A(member, MAX(10000, A(separator, member)));

	return A(
		S("{"),
		ws(),
		MAX(1, members),  // 0 or 1 occurrence of members
		ws(),
		S("}")
	);
}

// value := primitive | container
// primitive := number | string | bool | null
// container := array | object
static cma_op *json_value_impl(void) {
	return O(
		json_number(),
		json_string(),
		json_bool(),
		json_null(),
		json_array(),
		json_object()
	);
}

// Lazy reference to json_value_impl
static cma_op *json_value_ref_impl(cma_state *s, void *ctx) {
	(void)s;
	(void)ctx;
	// We'll set up the actual pattern after defining json_value_impl
	static cma_op *cached = NULL;
	if (!cached) cached = json_value_impl();
	return cached;
}

static cma_op *json_value_ref(void) {
	static cma_op_ref ref_storage = {0};
	return cma_init_ref(&ref_storage, json_value_ref_impl, NULL);
}


// ================================================================
//  Parsing with capture extraction
// ================================================================

// Extract string value from a capture, handling escapes
static char *unescape_string(const char *escaped, size_t len) {
	char *buf = malloc(len + 1);
	size_t out = 0;

	for (size_t i = 0; i < len; i++) {
		if (escaped[i] == '\\' && i + 1 < len) {
			i++;
			switch (escaped[i]) {
			case '"':  buf[out++] = '"'; break;
			case '\\': buf[out++] = '\\'; break;
			case '/':  buf[out++] = '/'; break;
			case 'b':  buf[out++] = '\b'; break;
			case 'f':  buf[out++] = '\f'; break;
			case 'n':  buf[out++] = '\n'; break;
			case 'r':  buf[out++] = '\r'; break;
			case 't':  buf[out++] = '\t'; break;
			case 'u':
				// TODO: proper unicode handling
				if (i + 4 < len) {
					// For now, just skip \uXXXX (would need utf8 encoding)
					i += 4;
				}
				break;
			default:
				buf[out++] = escaped[i];
			}
		} else {
			buf[out++] = escaped[i];
		}
	}
	buf[out] = '\0';
	return buf;
}

// Simple recursive descent parser that uses the pattern to validate
// and captures to extract values
typedef struct {
	const char *input;
	size_t len;
	size_t pos;
	cma_state cma;
} parser;

static void skip_ws(parser *p) {
	while (p->pos < p->len && isspace(p->input[p->pos]))
		p->pos++;
}

static json_value *parse_value(parser *p);

static json_value *parse_string(parser *p) {
	skip_ws(p);
	if (p->pos >= p->len || p->input[p->pos] != '"') return NULL;
	p->pos++;

	size_t start = p->pos;
	while (p->pos < p->len && p->input[p->pos] != '"') {
		if (p->input[p->pos] == '\\') p->pos++;
		p->pos++;
	}
	if (p->pos >= p->len) return NULL;

	size_t end = p->pos;
	p->pos++;  // skip closing quote

	char *raw = malloc(end - start + 1);
	memcpy(raw, &p->input[start], end - start);
	raw[end - start] = '\0';

	char *unescaped = unescape_string(raw, end - start);
	free(raw);

	json_value *v = malloc(sizeof(*v));
	v->type = JSON_STRING;
	v->value.string = unescaped;
	return v;
}

static json_value *parse_number(parser *p) {
	skip_ws(p);
	size_t start = p->pos;

	if (p->pos < p->len && p->input[p->pos] == '-') p->pos++;
	if (p->pos >= p->len || !isdigit(p->input[p->pos])) return NULL;

	while (p->pos < p->len && isdigit(p->input[p->pos]))
		p->pos++;

	if (p->pos < p->len && p->input[p->pos] == '.') {
		p->pos++;
		if (p->pos >= p->len || !isdigit(p->input[p->pos])) return NULL;
		while (p->pos < p->len && isdigit(p->input[p->pos]))
			p->pos++;
	}

	if (p->pos < p->len && (p->input[p->pos] == 'e' || p->input[p->pos] == 'E')) {
		p->pos++;
		if (p->pos < p->len && (p->input[p->pos] == '+' || p->input[p->pos] == '-'))
			p->pos++;
		if (p->pos >= p->len || !isdigit(p->input[p->pos])) return NULL;
		while (p->pos < p->len && isdigit(p->input[p->pos]))
			p->pos++;
	}

	char *num_str = malloc(p->pos - start + 1);
	memcpy(num_str, &p->input[start], p->pos - start);
	num_str[p->pos - start] = '\0';

	double num = strtod(num_str, NULL);
	free(num_str);

	json_value *v = malloc(sizeof(*v));
	v->type = JSON_NUMBER;
	v->value.number = num;
	return v;
}

static json_value *parse_array(parser *p) {
	skip_ws(p);
	if (p->pos >= p->len || p->input[p->pos] != '[') return NULL;
	p->pos++;

	json_value *arr = json_array_value();

	skip_ws(p);
	if (p->pos < p->len && p->input[p->pos] == ']') {
		p->pos++;
		return arr;
	}

	while (1) {
		json_value *item = parse_value(p);
		if (!item) {
			json_free(arr);
			return NULL;
		}
		json_array_push(arr, item);

		skip_ws(p);
		if (p->pos >= p->len) {
			json_free(arr);
			return NULL;
		}

		if (p->input[p->pos] == ']') {
			p->pos++;
			return arr;
		}

		if (p->input[p->pos] != ',') {
			json_free(arr);
			return NULL;
		}
		p->pos++;
	}
}

static json_value *parse_object(parser *p) {
	skip_ws(p);
	if (p->pos >= p->len || p->input[p->pos] != '{') return NULL;
	p->pos++;

	json_value *obj = json_object_value();

	skip_ws(p);
	if (p->pos < p->len && p->input[p->pos] == '}') {
		p->pos++;
		return obj;
	}

	while (1) {
		json_value *key_val = parse_string(p);
		if (!key_val) {
			json_free(obj);
			return NULL;
		}

		skip_ws(p);
		if (p->pos >= p->len || p->input[p->pos] != ':') {
			json_free(obj);
			json_free(key_val);
			return NULL;
		}
		p->pos++;

		json_value *val = parse_value(p);
		if (!val) {
			json_free(obj);
			json_free(key_val);
			return NULL;
		}

		json_object_set(obj, key_val->value.string, val);
		json_free(key_val);

		skip_ws(p);
		if (p->pos >= p->len) {
			json_free(obj);
			return NULL;
		}

		if (p->input[p->pos] == '}') {
			p->pos++;
			return obj;
		}

		if (p->input[p->pos] != ',') {
			json_free(obj);
			return NULL;
		}
		p->pos++;
	}
}

static json_value *parse_value(parser *p) {
	skip_ws(p);
	if (p->pos >= p->len) return NULL;

	char ch = p->input[p->pos];

	if (ch == '"') {
		return parse_string(p);
	} else if (ch == '{') {
		return parse_object(p);
	} else if (ch == '[') {
		return parse_array(p);
	} else if (ch == 't' || ch == 'f') {
		if (p->pos + 4 <= p->len && strncmp(&p->input[p->pos], "true", 4) == 0) {
			p->pos += 4;
			return json_bool_value(1);
		} else if (p->pos + 5 <= p->len && strncmp(&p->input[p->pos], "false", 5) == 0) {
			p->pos += 5;
			return json_bool_value(0);
		}
		return NULL;
	} else if (ch == 'n') {
		if (p->pos + 4 <= p->len && strncmp(&p->input[p->pos], "null", 4) == 0) {
			p->pos += 4;
			return json_null_value();
		}
		return NULL;
	} else if (ch == '-' || isdigit(ch)) {
		return parse_number(p);
	}

	return NULL;
}

// ================================================================
//  Public API
// ================================================================

json_value *json_parse(const char *input, size_t len) {
	parser p = {
		.input = input,
		.len = len,
		.pos = 0
	};

	json_value *result = parse_value(&p);
	if (!result) return NULL;

	skip_ws(&p);
	if (p.pos != p.len) {
		json_free(result);
		return NULL;  // didn't consume entire input
	}

	return result;
}

void json_print(json_value *v, int indent);

static void print_indent(int indent) {
	for (int i = 0; i < indent; i++)
		printf("  ");
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
			print_indent(indent + 1);
			json_print(v->value.array.items[i], indent + 1);
			if (i + 1 < v->value.array.count) printf(",");
			printf("\n");
		}
		print_indent(indent);
		printf("]");
		break;
	case JSON_OBJECT:
		printf("{\n");
		for (size_t i = 0; i < v->value.object.count; i++) {
			print_indent(indent + 1);
			printf("\"%s\": ", v->value.object.pairs[i].key);
			json_print(v->value.object.pairs[i].val, indent + 1);
			if (i + 1 < v->value.object.count) printf(",");
			printf("\n");
		}
		print_indent(indent);
		printf("}");
		break;
	}
}

// ================================================================
//  File I/O
// ================================================================

char *read_json(const char *filename, size_t *len) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		perror("fopen");
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		perror("ftell");
		fclose(f);
		return NULL;
	}

	char *buf = malloc(size + 1);
	if (!buf) {
		fprintf(stderr, "malloc failed\n");
		fclose(f);
		return NULL;
	}

	size_t nread = fread(buf, 1, size, f);
	fclose(f);

	if (nread != (size_t)size) {
		fprintf(stderr, "read mismatch\n");
		free(buf);
		return NULL;
	}

	buf[size] = '\0';
	*len = size;
	return buf;
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
	char *buf = read_json(argv[1], &len);
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
