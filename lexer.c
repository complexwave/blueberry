/*
 * lexer.c — Citrin lexer
 *
 * CMA-based tokenizer. Reads source file via syscalls into ci_str,
 * runs PEG grammar over it, dumps flat capture array with integer-tagged tokens.
 *
 * Included by parser.c — not compiled standalone.
 */

#define CI_STRING_TEST  /* suppress ciobj.c test main() */
#include "ciobj.c"
#include "cma/cma.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ================================================================
 *  Token types (X-macro)
 * ================================================================ */

#define TOKENS(X) \
	/* keywords */ \
	X(FUNCTION) \
	X(VAR) \
	X(IF) \
	X(ELSE) \
	X(WHILE) \
	X(FOR) \
	X(DO) \
	X(TRUE) \
	X(FALSE) \
	X(UNDEF) \
	X(NULL) \
	X(RETURN) \
	X(BREAK) \
	X(NEXT) \
	X(CONTINUE) \
	X(GOTO) \
	X(REQUIRE) \
	X(IN) \
	/* literals */ \
	X(INT) \
	X(HEX) \
	X(BIN) \
	X(DOUBLE) \
	X(STRING) \
	X(IDENTIFIER) \
	X(LABEL) \
	/* delimiters */ \
	X(SEMICOLON) \
	X(COMMA) \
	X(COLON) \
	X(CURLY_OPEN) \
	X(CURLY_CLOSE) \
	X(PAREN_OPEN) \
	X(PAREN_CLOSE) \
	X(BRACKET_OPEN) \
	X(BRACKET_CLOSE) \
	/* access */ \
	X(HASH_ACCESS) \
	X(MAP_ACCESS) \
	X(METHOD_REFERENCE) \
	/* 3-char operators */ \
	X(POW_ASSIGN) \
	X(LSHIFT_ASSIGN) \
	X(RSHIFT_ASSIGN) \
	X(OR_ASSIGN) \
	X(AND_ASSIGN) \
	X(NOTNULL_ASSIGN) \
	/* 2-char operators */ \
	X(HASHCOMMA) \
	X(EQ) \
	X(NEQ) \
	X(GT_EQ) \
	X(LT_EQ) \
	X(BIN_LSHIFT) \
	X(BIN_RSHIFT) \
	X(INCR) \
	X(DECR) \
	X(POW) \
	X(OR) \
	X(AND) \
	X(NOTNULL) \
	X(ADD_ASSIGN) \
	X(SUB_ASSIGN) \
	X(MUL_ASSIGN) \
	X(DIV_ASSIGN) \
	X(MOD_ASSIGN) \
	X(BIN_OR_ASSIGN) \
	X(BIN_AND_ASSIGN) \
	X(BIN_XOR_ASSIGN) \
	/* single-char operators */ \
	X(ASSIGNMENT) \
	X(ADD) \
	X(SUB) \
	X(MUL) \
	X(DIV) \
	X(MOD) \
	X(NOT) \
	X(GT) \
	X(LT) \
	X(BIN_OR) \
	X(BIN_AND) \
	X(BIN_INV) \
	X(BIN_XOR) \
	/* internal */ \
	X(NEWLINE) \
	/* error */ \
	X(ERR_UNKNOWN)

enum {
#define X_ENUM(name) L_##name,
	TOKENS(X_ENUM)
#undef X_ENUM
	L_COUNT
};

static const char *token_names[L_COUNT] = {
#define X_STR(name) [L_##name] = #name,
	TOKENS(X_STR)
#undef X_STR
};

/* ================================================================
 *  Grammar helpers
 * ================================================================ */

/* keyword: case-insensitive + word boundary (not followed by ident char) */
#define KW(str, id) CAPi(A( NOT(A(R("A-Za-z_0-9"), Si(str))), Si(str), NOT(R("A-Za-z_0-9"))), (size_t)(id) )

/* operator: literal string match */
#define OP(str, id) CAPi(S(str), (size_t)(id))

/* ================================================================
 *  Grammar
 * ================================================================ */

static cma_op *lexer_grammar(void) {
	/* ---- skip patterns ---- */
	cma_op *line_comment = A(S("//"), MIN(0, notCHRS('\n')));
	cma_op *hash_comment = A(S("#"), MIN(0, notCHRS('\n')));
	cma_op *ws_flat      = MIN(1, R(" \t\r"));
	cma_op *newline      = CAPi(S("\n"), (size_t)L_NEWLINE);
	cma_op *skip         = O(ws_flat, newline, line_comment, hash_comment);

	/* ---- strings ---- */
	cma_op *dq_char   = O(A(S("\\"), ANY(1)), notCHRS('"', '\\', '\n'));
	cma_op *sq_char   = O(A(S("\\"), ANY(1)), notCHRS('\'', '\\', '\n'));
	cma_op *dq_string = A(S("\""), CAPi(MIN(0, dq_char), (size_t)L_STRING), S("\""));
	cma_op *sq_string = A(S("'"),  CAPi(MIN(0, sq_char), (size_t)L_STRING), S("'"));

	/* ---- numbers (hex/bin/double/int separately tagged) ---- */
	cma_op *hex_num = CAPi(A(S("0x"), MIN(1, R("0-9A-Fa-f"))),              (size_t)L_HEX);
	cma_op *bin_num = CAPi(A(S("0b"), MIN(1, R("01"))),                     (size_t)L_BIN);
	cma_op *dbl_num = CAPi(A(MIN(1, R("0-9")), S("."), MIN(1, R("0-9"))),   (size_t)L_DOUBLE);
	cma_op *int_num = CAPi(MIN(1, R("0-9")),                                (size_t)L_INT);

	/* ---- identifier (catchall for names — must come after keywords) ---- */
	cma_op *ident = CAPi(A(R("A-Za-z_"), MIN(0, R("A-Za-z_0-9"))), (size_t)L_IDENTIFIER);
	cma_op *label = A(S(":"), CAPi(ident,(size_t)L_LABEL), S(":"));
	
	/* ---- error catchall: single byte ---- */
	cma_op *error = CAPi(ANY(1), (size_t)L_ERR_UNKNOWN);

	/* ---- token: ordered choice ----
	 * Longer/more-specific patterns before shorter ones.
	 * Keywords before identifier. Multi-char ops before single-char.
	 */
	cma_op *token = O(
		/* keywords */
		KW("function", L_FUNCTION),
		KW("fun",      L_FUNCTION),
		KW("fn",       L_FUNCTION),
		KW("var",      L_VAR),
		KW("if",       L_IF),
		KW("else",     L_ELSE),
		KW("while",    L_WHILE),
		KW("for",      L_FOR),
		KW("do",       L_DO),
		KW("true",     L_TRUE),
		KW("false",    L_FALSE),
		KW("undef",    L_UNDEF),
		KW("null",     L_NULL),
		KW("return",   L_RETURN),
		KW("break",    L_BREAK),
		KW("next",     L_NEXT),
		KW("continue", L_NEXT),
		KW("goto",     L_GOTO),
		KW("in",  L_IN),
		skip,
		
		/* strings */
		dq_string,
		sq_string,
		
		/* numbers (hex/bin before decimal, double before int) */
		hex_num,
		bin_num,
		dbl_num,
		int_num,

		/* 3-char operators (before 2-char!) */
		OP("**=", L_POW_ASSIGN),
		OP("<<=", L_LSHIFT_ASSIGN),
		OP(">>=", L_RSHIFT_ASSIGN),
		OP("||=", L_OR_ASSIGN),
		OP("&&=", L_AND_ASSIGN),
		OP("?" "?=", L_NOTNULL_ASSIGN),

		/* 2-char operators (before 1-char!) */
		OP("->", L_MAP_ACCESS),
		OP("<-", L_METHOD_REFERENCE),
		OP("=>", L_HASHCOMMA),
		OP("==", L_EQ),
		OP("!=", L_NEQ),
		OP(">=", L_GT_EQ),
		OP("<=", L_LT_EQ),
		OP("<<", L_BIN_LSHIFT),
		OP(">>", L_BIN_RSHIFT),
		OP("++", L_INCR),
		OP("--", L_DECR),
		OP("**", L_POW),
		OP("?" "?", L_NOTNULL),
		OP("?=", L_NOTNULL_ASSIGN),
		OP("||", L_OR),
		OP("&&", L_AND),
		OP("+=", L_ADD_ASSIGN),
		OP("-=", L_SUB_ASSIGN),
		OP("*=", L_MUL_ASSIGN),
		OP("/=", L_DIV_ASSIGN),
		OP("%=", L_MOD_ASSIGN),
		OP("|=", L_BIN_OR_ASSIGN),
		OP("&=", L_BIN_AND_ASSIGN),
		OP("^=", L_BIN_XOR_ASSIGN),

		label,
		
		/* single-char */
		OP(";", L_SEMICOLON),
		OP(",", L_COMMA),
		OP(":", L_COLON),
		OP("{", L_CURLY_OPEN),
		OP("}", L_CURLY_CLOSE),
		OP("(", L_PAREN_OPEN),
		OP(")", L_PAREN_CLOSE),
		OP("[", L_BRACKET_OPEN),
		OP("]", L_BRACKET_CLOSE),
		OP(".", L_HASH_ACCESS),
		OP("=", L_ASSIGNMENT),
		OP("+", L_ADD),
		OP("-", L_SUB),
		OP("*", L_MUL),
		OP("/", L_DIV),
		OP("%", L_MOD),
		OP("!", L_NOT),
		OP(">", L_GT),
		OP("<", L_LT),
		OP("|", L_BIN_OR),
		OP("&", L_BIN_AND),
		OP("~", L_BIN_INV),
		OP("^", L_BIN_XOR),

		/* identifier */
		ident,

		/* error — consumes one byte, keeps lexer moving */
		error
	);

	return A(MIN(0, token), ENDL());
}


/* ================================================================
 *  Parser types
 * ================================================================ */

typedef struct {
	cma_state cma;
	ci_str   *buf;
	uint32_t  pos;    /* current capture index */
} b_parser;

typedef struct {
	const char *data;
	uint32_t    len;
	uint32_t    t;    /* token type (L_*) */
} b_tok;

static const b_tok b_tok_eof = { NULL, 0, L_ERR_UNKNOWN };

#define b_parser_pos(p) ((p)->pos)

/* ================================================================
 *  File I/O (Linux syscalls, no stdio)
 * ================================================================ */

static ci_str *read_file_to_cistr(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;

	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return NULL;
	}

	ci_str *buf = ci_str_new((size_t)st.st_size);
	if (!buf) {
		close(fd);
		return NULL;
	}

	uint8_t *tail = ci_str_ensure_tail(buf, (size_t)st.st_size);
	ssize_t n = read(fd, tail, (size_t)st.st_size);
	close(fd);

	if (n < 0) {
		ci_free(buf);
		return NULL;
	}
	ci_str_put_tail(buf, (size_t)n);
	return buf;
}

/* ================================================================
 *  Parser API
 * ================================================================ */

static b_parser *b_parser_new(void) {
	b_parser *p = malloc(sizeof(b_parser));
	if (!p) return NULL;
	memset(p, 0, sizeof(*p));
	return p;
}

static int b_parser_load_buf(b_parser *p, ci_str *buf) {
	p->buf = buf;
	p->pos = 0;
	size_t len = ci_str_len(buf);
	cma_init(&p->cma, (const char *)ci_str_head(buf), len);
	p->cma.flags |= CMA_END_IS_MISS;

	static cma_op *grammar = NULL;
	if (!grammar) grammar = lexer_grammar();

	return cma_match(&p->cma, grammar) >= 0;
}

static int b_parser_load_file(b_parser *p, const char *filename) {
	ci_str *buf = read_file_to_cistr(filename);
	if (!buf) return 0;
	int r = b_parser_load_buf(p, buf);
	ci_dec(buf);
	
	return r;
}

static b_tok b_parser_peek(b_parser *p) {
	uint32_t count = cma_cap_count(&p->cma);
	uint32_t i = p->pos;
	while (i < count) {
		cma_capture *c = cma_cap_get(&p->cma, i);
		size_t id = cma_cap_id(c);

		if (id != L_NEWLINE) {
			return (b_tok){
				.data = (const char *)c->start,
				.len  = (uint32_t)cma_cap_len(c),
				.t    = (uint32_t)id,
			};
		}
		i++;
	}
	return b_tok_eof;
}

static b_tok b_parser_next(b_parser *p) {
	uint32_t count = cma_cap_count(&p->cma);
	while (p->pos < count) {
		cma_capture *c = cma_cap_get(&p->cma, p->pos++);
		size_t id = cma_cap_id(c);
		if (id != L_NEWLINE) {
			return (b_tok){
				.data = (const char *)c->start,
				.len  = (uint32_t)cma_cap_len(c),
				.t    = (uint32_t)id,
			};
		}
	}
	return b_tok_eof;
}

static b_tok b_parser_try_next(b_parser *p, uint32_t id) {
	uint32_t count = cma_cap_count(&p->cma);
	uint32_t i = p->pos;
	while (i < count) {
		cma_capture *c = cma_cap_get(&p->cma, i);
		size_t cap_id = cma_cap_id(c);
		if (cap_id != L_NEWLINE) {
			if ((uint32_t)cap_id != id)
				return b_tok_eof;
			p->pos = i + 1;
			return (b_tok){
				.data = (const char *)c->start,
				.len  = (uint32_t)cma_cap_len(c),
				.t    = (uint32_t)cap_id,
			};
		}
		i++;
	}
	return b_tok_eof;
}

static uint32_t b_parser_line_of(b_parser *p, const char *char_pos) {
	uint32_t line = 1;
	uint32_t count = cma_cap_count(&p->cma);
	for (uint32_t i = 0; i < count; i++) {
		cma_capture *c = cma_cap_get(&p->cma, i);
		if (cma_cap_id(c) != L_NEWLINE) continue;
		if ((const char *)c->start >= char_pos) break;
		line++;
	}
	return line;
}

static void b_parser_free(b_parser *p) {
	cma_free(&p->cma);
	if (p->buf) ci_free(p->buf);
	free(p);
}
