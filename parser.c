/*
 * parser.c — Citrin parser + AST
 *
 * Build:
 *   gcc -Wall -Wextra -Wno-missing-braces -std=c11 -g -DCI_LEXER \
 *       -o parser parser.c cma/cma.c
 *
 * Usage:
 *   ./parser <source-file>
 */

#include "lexer.c"

/* ================================================================
 *  AST types
 * ================================================================ */

typedef struct ast_node ast_node;
typedef struct ast      ast;

static int ast_debug = 0;

#define AST_KINDS(X) \
	X(NUMBER)     \
	X(STRING)     \
	X(IDENTIFIER) \
	X(IF)         \
	X(LOOP)       \
	X(FOR_LOOP)       \
	X(DO_LOOP)    \
	X(FUNCTION)   \
	X(VAR)        \
	X(RETURN)     \
	X(BREAK)      \
	X(NEXT)       \
	X(LABEL)      \
	X(GOTO)       \
	X(REQUIRE)    \
	X(CODEBLOCK)  \
	X(ADD)        \
	X(SUB)        \
	X(MUL)        \
	X(DIV)        \
	X(MOD)        \
	X(POW)        \
	X(EQ)         \
	X(NEQ)        \
	X(GT)         \
	X(LT)         \
	X(GT_EQ)      \
	X(LT_EQ)      \
	X(OR)         \
	X(AND)        \
	X(BIN_OR)     \
	X(BIN_AND)    \
	X(BIN_XOR)    \
	X(BIN_LSHIFT) \
	X(BIN_RSHIFT) \
	X(INFIX_OP)   \
	X(PREFIX_OP)  \
	X(HASHACCESS)  \
	X(MAPACCESS)  \
	X(ARRACCESS)  \
	X(ASSIGN)     \
	X(ASSIGN_ADD) \
	X(ASSIGN_SUB) \
	X(ASSIGN_MUL) \
	X(ASSIGN_DIV) \
	X(ASSIGN_MOD) \
	X(ASSIGN_POW) \
	X(ASSIGN_OR)  \
	X(ASSIGN_AND) \
	X(ASSIGN_NOTNULL) \
	X(ASSIGN_BIN_OR) \
	X(ASSIGN_BIN_AND) \
	X(ASSIGN_BIN_XOR) \
	X(ASSIGN_LSHIFT) \
	X(ASSIGN_RSHIFT) \
	X(NOTNULL)    \
	X(MINUS)      \
	X(NOT)        \
	X(BIN_INV)    \
	X(INC)        \
	X(DEC)        \
	X(POST_INC)   \
	X(POST_DEC)   \
	X(MAP_GET) \
	X(METHOD_REF) \
	X(TRUE)       \
	X(FALSE)      \
	X(NULL_LIT)   \
	X(ARRAY_INIT) \
	X(MAP_INIT)   \
	X(CALL)       \
	X(EXPRESSION) \
	X(EXPRLIST) \

enum {
	A__INVALID = 0,
#define X_ENUM(name) A_##name,
	AST_KINDS(X_ENUM)
#undef X_ENUM
	A_COUNT
};

static const char *ast_kind_names[A_COUNT] = {
	[0] = "(invalid)",
#define X_STR(name) [A_##name] = #name,
	AST_KINDS(X_STR)
#undef X_STR
};

/* bits 0-15: type of op, bits 16-18: arg count, bit 19: list, bits 20+: subtype */
#define A_TYPE(t)     ((t) & 0xFFFF)
#define A_SUBTYPE(t)  ((t) >> 20)

/* arg count: 3-bit field in bits 16-18 (0-7 fixed children) */
#define A_ARG_SHIFT   16
#define A_ARG_MASK    (7U << A_ARG_SHIFT)
#define A_ARG_CNT(t)  (((t) & A_ARG_MASK) >> A_ARG_SHIFT)

#define A_ARG_0       (0U << A_ARG_SHIFT)
#define A_ARG_1       (1U << A_ARG_SHIFT)
#define A_ARG_2       (2U << A_ARG_SHIFT)
#define A_ARG_3       (3U << A_ARG_SHIFT)
#define A_ARG_4       (4U << A_ARG_SHIFT)
#define A_ARG_5       (5U << A_ARG_SHIFT)

/* list: variable-length children via ci_array (separate from arg count) */
#define A_LIST        (1U << 19)

/* aliases */
#define A_PREFIX      A_ARG_1
#define A_POSTFIX     A_ARG_1
#define A_INFIX       A_ARG_2

/* number subtypes */
#define A_NUMBER_INT    (1U << 20)
#define A_NUMBER_HEX    (1U << 21)
#define A_NUMBER_BIN    (1U << 22)
#define A_NUMBER_DOUBLE (1U << 23)

/* loop subtypes */
#define A_DO_LOOP       (1U << 20)

struct ast_node {
	uint32_t   type;    /* A_TYPE | arg count | A_LIST | subtype */
	ast       *parent;
	b_tok      token;   /* source token for this node */
	union {
		ci_array  *nodes;      /* A_LIST: variable-length children */
		double     num_double;
		uint64_t   num_int;
		ast_node  *args[1];    /* A_ARG_N: fixed children (variable-size alloc) */

		/* named overlays — alias args[0..N], for readability */
		struct {                    /* A_ARG_3 */
			ast_node *condition;
			ast_node *body;
			ast_node *else_body;
		} op_if;

		struct {                    /* A_ARG_4 */
			ast_node *init;
			
			ast_node *condition;
			
			union {
				ast_node *step;
				ast_node *iterator_vars;
			};
			
			ast_node *body;
		} op_loop;
		
		struct {                    /* A_ARG_3 */
			ast_node *name;
			ast_node *args;
			ast_node *body;
			uint32_t function_id;
		} op_function;
	};
};

struct ast {
	b_parser *p;
};

static ast *ast_new(b_parser *p) {
	ast *a = malloc(sizeof(ast));
	if (!a) return NULL;
	a->p = p;
	return a;
}

static size_t ast_node_size(uint32_t type) {
	if (type & A_LIST) {
		return offsetof(ast_node, args) + sizeof(ci_array *);
	}
	uint32_t cnt = A_ARG_CNT(type);
	uint32_t slots = cnt > 0 ? cnt : 1; /* minimum 1 for num_double/num_int */
	return offsetof(ast_node, args) + slots * sizeof(ast_node *);
}

static ast_node *ast_newnode(ast *a, uint32_t type, b_tok token) {
	size_t sz = sizeof(ast_node);
	ast_node *n = malloc(sz);
	if (!n) return NULL;
	memset(n, 0, sz);
	n->type   = type;
	n->parent = a;
	n->token  = token;
	if (type & A_LIST) {
		n->nodes = ci_arr_new(4);
	}
	return n;
}

static void ast_node_push(ast_node *n, ast_node *child) {
	if (!(n->type & A_LIST)) {
		fprintf(stderr, "error: ast_node_push on non-list node\n");
		return;
	}
	ci_arr_push(n->nodes, (ci_ptr)child);
}

static ast_node *ast_node_list(ast_node *n, uint32_t idx) {
	if (!(n->type & A_LIST)) {
		fprintf(stderr, "error: accessing non-list node as list\n");
		return NULL;
	}
	return (ast_node *)ci_arr_index(n->nodes, idx);
}

static uint32_t ast_node_list_length(ast_node *n) {
	if (!(n->type & A_LIST)) {
		fprintf(stderr, "error: list length on non-list node\n");
		return 0;
	}
	return ci_arr_len(n->nodes);
}

static void ast_node_free(ast_node *n) {
	if (!n) return;
	if (n->type & A_LIST) {
		for (uint32_t i = 0; i < ci_arr_len(n->nodes); i++) {
			ast_node_free((ast_node *)ci_arr_index(n->nodes, i));
		}
		n->nodes->length = 0;
		ci_dec(n->nodes);
	} else {
		uint32_t cnt = A_ARG_CNT(n->type);
		for (uint32_t i = 0; i < cnt; i++) {
			ast_node_free(n->args[i]);
		}
	}
	free(n);
}

static void ast_free(ast *a) { free(a); }

/* ================================================================
 *  Parse functions
 * ================================================================ */

/* ---- forward declarations ---- */
static ast_node *ast_A_NUMBER(ast *a);
static ast_node *ast_A_STRING(ast *a);
static ast_node *ast_A_IDENTIFIER(ast *a);
static ast_node *ast_A_EXPRESSION(ast *a);

static ast_node *ast_consume_expression(ast *a);
static ast_node *ast_consume_expression_list(ast *a);
static ast_node *ast_consume_function_name(ast *a);

/* ---- primitives ---- */

static ast_node *ast_A_NUMBER(ast *a) {
	b_tok tok;
	if ((tok = b_parser_try_next(a->p, L_HEX)).data) {
		ast_node *n = ast_newnode(a, A_NUMBER | A_NUMBER_HEX, tok);
		if (n) n->num_int = strtoull(tok.data + 2, NULL, 16);
		return n;
	}
	if ((tok = b_parser_try_next(a->p, L_BIN)).data) {
		ast_node *n = ast_newnode(a, A_NUMBER | A_NUMBER_BIN, tok);
		if (n) n->num_int = strtoull(tok.data + 2, NULL, 2);
		return n;
	}
	if ((tok = b_parser_try_next(a->p, L_DOUBLE)).data) {
		ast_node *n = ast_newnode(a, A_NUMBER | A_NUMBER_DOUBLE, tok);
		if (n) n->num_double = strtod(tok.data, NULL);
		return n;
	}
	if ((tok = b_parser_try_next(a->p, L_INT)).data) {
		ast_node *n = ast_newnode(a, A_NUMBER | A_NUMBER_INT, tok);
		if (n) n->num_int = strtoull(tok.data, NULL, 10);
		return n;
	}
	return NULL;
}

static ast_node *ast_A_STRING(ast *a) {
	b_tok tok = b_parser_try_next(a->p, L_STRING);
	if (!tok.data) return NULL;
	return ast_newnode(a, A_STRING, tok);
}

static ast_node *ast_A_IDENTIFIER(ast *a) {
	b_tok tok = b_parser_try_next(a->p, L_IDENTIFIER);
	if (!tok.data) return NULL;
	return ast_newnode(a, A_IDENTIFIER, tok);
}

static ast_node *ast_ident2str(ast_node *node) {
	if(node->type == A_IDENTIFIER){
		node->type = A_STRING;
	}
	
	return node;
}

/* ---- priority-based expression parsing ---- */

typedef struct ast_op_entry ast_op_entry;

typedef ast_node *(*ast_prefix_fn)(ast *a, const ast_op_entry *entry);
typedef ast_node *(*ast_infix_fn)(ast *a, ast_node *left, const ast_op_entry *entry);

struct ast_op_entry {
	uint32_t      prio;
	uint32_t      token_type;
	ast_prefix_fn prefix;
	uint32_t      prefix_type;
	ast_infix_fn  infix;
	uint32_t      infix_type;
	uint32_t      closing_token;
};

static ast_node *ast_expr_bp(ast *a, uint32_t max_prio);

/* ---- prefix callbacks ---- */

static ast_node *ast_prefix_number(ast *a, const ast_op_entry *entry) {
	(void)entry;
	return ast_A_NUMBER(a);
}

static ast_node *ast_prefix_string(ast *a, const ast_op_entry *entry) {
	(void)entry;
	return ast_A_STRING(a);
}

static ast_node *ast_prefix_identifier(ast *a, const ast_op_entry *entry) {
	(void)entry;
	return ast_A_IDENTIFIER(a);
}

static ast_node *ast_prefix_keyword(ast *a, const ast_op_entry *entry) {
	b_tok tok = b_parser_try_next(a->p, entry->token_type);
	if (!tok.data) return NULL;
	return ast_newnode(a, entry->prefix_type, tok);
}

static ast_node *ast_simple_prefix(ast *a, const ast_op_entry *entry) {
	b_tok op = b_parser_try_next(a->p, entry->token_type);
	if (!op.data) return NULL;

	ast_node *operand = ast_expr_bp(a, entry->prio);
	if (!operand) return NULL;

	uint32_t at = entry->prefix_type ? entry->prefix_type : A_PREFIX_OP;
	ast_node *n = ast_newnode(a, at | A_PREFIX, op);
	if (!n) {
		ast_node_free(operand);
		return NULL;
	}
	n->args[0] = operand;
	return n;
}

// accept no operand
static ast_node *ast_statement_return(ast *a, const ast_op_entry *entry) {
	b_tok op = b_parser_try_next(a->p, entry->token_type);
	if (!op.data) return NULL;

	ast_node *operand = ast_expr_bp(a, entry->prio);
	
	uint32_t at = entry->prefix_type ? entry->prefix_type : A_PREFIX_OP;
	ast_node *n = ast_newnode(a, at | A_PREFIX, op);
	if (!n) {
		ast_node_free(operand);
		return NULL;
	}
	n->args[0] = operand;
	return n;
}


static void ast_error(ast *a, const char *msg) {
	fprintf(stderr, "error: %s", msg);

	b_tok cur = b_parser_peek(a->p);
	if (cur.data) {
		uint32_t line = b_parser_line_of(a->p, cur.data);
		const char *got = (cur.t < L_COUNT) ? token_names[cur.t] : "???";
		fprintf(stderr, " (got %s at line %u)\n", got, line);

		const char *src = (const char *)ci_str_head(a->p->buf);
		const char *end = src + ci_str_len(a->p->buf);
		const char *lstart = cur.data;
		while (lstart > src && lstart[-1] != '\n') lstart--;
		const char *lend = cur.data;
		while (lend < end && *lend != '\n') lend++;
		fprintf(stderr, "  %.*s\n", (int)(lend - lstart), lstart);
		int col = (int)(cur.data - lstart);
		fprintf(stderr, "  %*s^\n", col, "");
	} else {
		fprintf(stderr, " at end of input\n");
	}
}

static b_tok ast_expect_token(ast *a, uint32_t token_type) {
	b_tok tok = b_parser_try_next(a->p, token_type);
	if (!tok.data) {
		const char *expected = (token_type < L_COUNT) ? token_names[token_type] : "???";
		char buf[512];
		snprintf(buf, sizeof(buf), "expected %s", expected);
		ast_error(a, buf);
	}
	return tok;
}

/* prefix: (expr), [expr,...], {expr,...} — wraps inner expression between delimiters.
 * If prefix_type is set, wraps result in a typed list node (array/map init).
 * If prefix_type is 0, returns inner expression as-is (paren grouping). */
static ast_node *ast_wrapping_expression(ast *a, const ast_op_entry *entry) {
	b_tok opening = b_parser_try_next(a->p, entry->token_type);
	if (!opening.data) return NULL;

	ast_node *inner = ast_expr_bp(a, UINT32_MAX);

	b_tok closing = ast_expect_token(a, entry->closing_token);
	if (!closing.data) {
		ast_node_free(inner);
		return NULL;
	}

	/* typed literal: [items] or {items} — wrap in a list node */
	if (entry->prefix_type) {
		ast_node *n = ast_newnode(a, entry->prefix_type | A_LIST, opening);
		if (!n) { ast_node_free(inner); return NULL; }
		if (inner) {
			/* flatten comma list into children, or push single item */
			if (A_TYPE(inner->type) == A_EXPRLIST && (inner->type & A_LIST)) {
				for (uint32_t i = 0; i < ast_node_list_length(inner); i++)
					ast_node_push(n, ast_node_list(inner, i));
				/* detach children so freeing inner doesn't recurse */
				ci_arr_clear(inner->nodes);
				ast_node_free(inner);
			} else {
				ast_node_push(n, inner);
			}
		}
		return n;
	}

	/* paren grouping: return inner as-is */
	if (!inner)
		return ast_newnode(a, A_EXPRLIST | A_LIST, opening);
	return inner;
}

/* infix: left[expr], subscript / indexing with delimiters */

static ast_node *_ast_wrapping_infix(ast *a, ast_node *left, const ast_op_entry *entry, uint32_t accept_empty) {
	b_tok op = b_parser_try_next(a->p, entry->token_type);
	if (!op.data) return NULL;

	ast_node *index = ast_expr_bp(a, UINT32_MAX);
	if (!accept_empty && !index) return NULL;

	b_tok closing = ast_expect_token(a, entry->closing_token);
	if (!closing.data) {
		ast_node_free(index);
		return NULL;
	}

	uint32_t at = entry->infix_type ? entry->infix_type : A_INFIX_OP;
	ast_node *n = ast_newnode(a, at | A_INFIX, op);
	if (!n) {
		ast_node_free(index);
		return NULL;
	}
	n->args[0] = left;
	n->args[1] = index;
	return n;
}


static ast_node *ast_wrapping_infix(ast *a, ast_node *left, const ast_op_entry *entry) {
	return _ast_wrapping_infix(a, left, entry, 0);
}

static ast_node *ast_wrapping_empty_infix(ast *a, ast_node *left, const ast_op_entry *entry) {
	return _ast_wrapping_infix(a, left, entry, 1);
}



static ast_node *ast_simple_postfix(ast *a, ast_node *left, const ast_op_entry *entry) {
	b_tok op = b_parser_try_next(a->p, entry->token_type);
	if (!op.data) return NULL;
	
	uint32_t at = entry->infix_type ? entry->infix_type : A_POSTFIX;
	ast_node *n = ast_newnode(a, at | A_ARG_1, op);
	if (!n) {
		return NULL;
	}
	
	n->args[0] = left;
	
	return n;
}

/* ---- infix callbacks ---- */

static ast_node *ast_simple_infix(ast *a, ast_node *left, const ast_op_entry *entry) {
	b_tok op = b_parser_try_next(a->p, entry->token_type);
	if (!op.data) return NULL;

	ast_node *right = ast_expr_bp(a, entry->prio);
	if (!right) {
		/* TODO: error recovery? for now just fail */
		return NULL;
	}

	uint32_t at = entry->infix_type ? entry->infix_type : A_INFIX_OP;
	ast_node *n = ast_newnode(a, at | A_INFIX, op);
	if (!n) {
		ast_node_free(right);
		return NULL;
	}
	n->args[0] = left;
	n->args[1] = right;
	return n;
}


static ast_node *ast_strkey_infix(ast *a, ast_node *left, const ast_op_entry *entry) {
	b_tok op = b_parser_try_next(a->p, entry->token_type);
	if (!op.data) return NULL;

	ast_node *right = ast_expr_bp(a, entry->prio);
	if (!right) {
		/* TODO: error recovery? for now just fail */
		return NULL;
	}

	uint32_t at = entry->infix_type ? entry->infix_type : A_INFIX_OP;
	ast_node *n = ast_newnode(a, at | A_INFIX, op);
	if (!n) {
		ast_node_free(right);
		return NULL;
	}
	n->args[0] = left;
	n->args[1] = ast_ident2str(right);
	return n;
}


static ast_node *ast_hash_access(ast *a, ast_node *left, const ast_op_entry *entry) {
	b_tok op = b_parser_try_next(a->p, entry->token_type);
	if (!op.data) return NULL;

	ast_node *n = ast_newnode(a, A_HASHACCESS | A_LIST, op);
	if (!n) return NULL;

	ast_node_push(n, left);

	for (;;) {
		ast_node *right = ast_expr_bp(a, entry->prio);
		if (!right) break;

		// change type of identifier to string
		right = ast_ident2str(right);
		
		ast_node_push(n, right);
		/* try consume next dot in chained map.key.key2 */
		b_tok next = b_parser_try_next(a->p, entry->token_type);
		if (!next.data) break;
	}

	if (ast_node_list_length(n) < 2) {
		fprintf(stderr, "error: hash access with no key\n");
		ast_node_free(n);
		return NULL;
	}

	return n;
}


static ast_node *ast_comma_list(ast *a, ast_node *left, const ast_op_entry *entry) {
	ast_node *n = ast_newnode(a, A_EXPRLIST | A_LIST, (b_tok){.t = 0});
	if (!n) return NULL;

	ast_node *expr = left;
	
	for (;;) {
		b_tok next = b_parser_try_next(a->p, L_HASHCOMMA);
		if (next.data) {
			ast_ident2str(expr);
		}else {
			next = b_parser_try_next(a->p, L_COLON);
			if (next.data) {
				ast_ident2str(expr);
			}
		}
		
		if(!next.data) {
			next = b_parser_try_next(a->p, L_COMMA);
			if (!next.data) break;
		}
		
		ast_node_push(n, expr);

		expr = ast_expr_bp(a, entry->prio);
		if (!expr) break;
		
	}
	
	ast_node_push(n, expr);
	
	return n;
}

// dedicated parsers

static ast_node *ast_codelist(ast *a) {
	ast_node *n = ast_newnode(a, A_CODEBLOCK | A_LIST, (b_tok){.t = L_CURLY_OPEN});
	if (!n) return NULL;

	for (;;) {
		ast_node *code = ast_expr_bp(a, UINT32_MAX);
		if (!code) break;
		
		ast_node_push(n, code);
		
		// optional semicolon
		b_parser_try_next(a->p, L_SEMICOLON);
	}
	
	return n;
}

static ast_node *ast_codeblock(ast *a) {
	// consume {
	b_tok op = b_parser_try_next(a->p, L_CURLY_OPEN);
	if (!op.data) return NULL;

	ast_node *n = ast_codelist(a);

	b_tok closing = ast_expect_token(a, L_CURLY_CLOSE);
	if (!closing.data) {
		ast_node_free(n);
		return NULL;
	}
	
	return n;
}

static ast_node *ast_statement_if(ast *a, const ast_op_entry *entry) {
	(void)entry;
	
	b_tok op = b_parser_try_next(a->p, L_IF);
	if (!op.data) return NULL;
	
	ast_node *n = ast_newnode(a, A_IF | A_ARG_3, op);
	if (!n) return NULL;

	n->op_if.condition = ast_consume_expression(a);
	if (!n->op_if.condition) {
		fprintf(stderr, "error: expected condition after 'if'\n");
		ast_node_free(n);
		return NULL;
	}

	n->op_if.body = ast_codeblock(a);
	if (!n->op_if.body) {
		ast_node_free(n);
		return NULL;
	}
	
	op = b_parser_try_next(a->p, L_ELSE);
	if (!op.data) {
		return n;
	}
	
	// try chaining if else
	n->op_if.else_body = ast_statement_if(a, entry);
	if (n->op_if.else_body) {
		return n;
	}
	
	n->op_if.else_body = ast_codeblock(a);
	if (!n->op_if.else_body) {
		ast_error(a, "expected code block after 'else'");
		ast_node_free(n);
		return NULL;
	}
	
	return n;
}

static ast_node *ast_statement_while(ast *a, const ast_op_entry *entry) {
	(void)entry;

	b_tok op = ast_expect_token(a, L_WHILE);
	if (!op.data) return NULL;

	ast_node *n = ast_newnode(a, A_LOOP | A_ARG_4, op);
	if (!n) return NULL;

	n->op_loop.condition = ast_consume_expression(a);
	if (!n->op_loop.condition) {
		ast_error(a, "expected condition after 'while'");
		ast_node_free(n);
		return NULL;
	}

	n->op_loop.body = ast_codeblock(a);
	if (!n->op_loop.body) {
		ast_error(a, "expected code block after 'while' condition");
		ast_node_free(n);
		return NULL;
	}

	return n;
}

static ast_node *ast_statement_for(ast *a, const ast_op_entry *entry) {
	(void)entry;

	b_tok op = ast_expect_token(a, L_FOR);
	if (!op.data) return NULL;

	ast_node *n = ast_newnode(a, A_FOR_LOOP | A_ARG_4, op);
	if (!n) return NULL;

	
	n->op_loop.iterator_vars = ast_consume_expression_list(a);
	if (!n->op_loop.iterator_vars) {
		ast_error(a, "expected iterator_vars after 'for'");
		ast_node_free(n);
		return NULL;
	}
	
	op = ast_expect_token(a, L_IN);
	if (!op.data) {
		ast_error(a, "expected IN after 'for'");
		ast_node_free(n);
		return NULL;
	}
	
	n->op_loop.init = ast_consume_expression_list(a);
	if (!n->op_loop.iterator_vars) {
		ast_error(a, "expected iterable after 'for in'");
		ast_node_free(n);
		return NULL;
	}
	
	
	n->op_loop.body = ast_codeblock(a);
	if (!n->op_loop.body) {
		ast_error(a, "expected code block after 'for' condition");
		ast_node_free(n);
		return NULL;
	}

	return n;
}

static ast_node *ast_statement_function(ast *a, const ast_op_entry *entry) {
	(void)entry;

	b_tok op = ast_expect_token(a, L_FUNCTION);
	if (!op.data) return NULL;

	ast_node *n = ast_newnode(a, A_FUNCTION | A_ARG_3, op);
	if (!n) return NULL;

	n->op_function.name = ast_consume_function_name(a);
	
	n->op_function.args = ast_consume_expression_list(a);
	if (!n->op_function.args) {
		ast_error(a, "expected arguments after function declaration");
		ast_node_free(n);
		return NULL;
	}

	n->op_function.body = ast_codeblock(a);
	if (!n->op_function.body) {
		ast_error(a, "expected code block after function declaration");
		ast_node_free(n);
		return NULL;
	}

	return n;
}

static ast_node *ast_statement_do(ast *a, const ast_op_entry *entry) {
	(void)entry;

	b_tok op = ast_expect_token(a, L_DO);
	if (!op.data) return NULL;

	ast_node *n = ast_newnode(a, A_LOOP | A_DO_LOOP | A_ARG_4, op);
	if (!n) return NULL;

	n->op_loop.body = ast_codeblock(a);
	if (!n->op_loop.body) {
		ast_error(a, "expected code block after 'do'");
		ast_node_free(n);
		return NULL;
	}

	b_tok w = ast_expect_token(a, L_WHILE);
	if (!w.data) {
		ast_node_free(n);
		return NULL;
	}

	n->op_loop.condition = ast_consume_expression(a);
	if (!n->op_loop.condition) {
		ast_error(a, "expected condition after 'do ... while'");
		ast_node_free(n);
		return NULL;
	}

	return n;
}





/* ---- unified dispatch table ---- */

/* lower priority = more sticky (binds tighter)
 * higher priority = less sticky (consumes more)
 * 0-100: atoms, 100-200: expressions, 200+: statements */
static const ast_op_entry as_ops_prio[] = {
	/* prio   token            prefix_fn                 pfx_type      infix_fn                  ifx_type       close */
	{  10,    L_HEX,           ast_prefix_number,        0,            NULL,                     0,             0              },
	{  10,    L_BIN,           ast_prefix_number,        0,            NULL,                     0,             0              },
	{  10,    L_DOUBLE,        ast_prefix_number,        0,            NULL,                     0,             0              },
	{  10,    L_INT,           ast_prefix_number,        0,            NULL,                     0,             0              },
	{  10,    L_STRING,        ast_prefix_string,        0,            NULL,                     0,             0              },
	{  10,    L_IDENTIFIER,    ast_prefix_identifier,    0,            NULL,                     0,             0              },
	{  10,    L_TRUE,          ast_prefix_keyword,       A_TRUE,       NULL,                     0,             0              },
	{  10,    L_FALSE,         ast_prefix_keyword,       A_FALSE,      NULL,                     0,             0              },
	{  10,    L_NULL,          ast_prefix_keyword,       A_NULL_LIT,   NULL,                     0,             0              },

	{  10,    L_BREAK,         ast_prefix_keyword,       A_BREAK,      NULL,                     0,             0              },
	{  10,    L_NEXT,          ast_prefix_keyword,       A_NEXT,       NULL,                     0,             0              },
	{  10,    L_LABEL,         ast_prefix_keyword,       A_LABEL,       NULL,                    0,             0              },
	
#define A_VALUE_PRIO 100

	{ 105,    L_HASH_ACCESS,   NULL,                     0,            ast_hash_access,          A_HASHACCESS,  0              },
#define A_VALUE_FN_NAME 106
	
	/* method access */
	{ 105,    L_MAP_ACCESS,       NULL,                  0,            ast_strkey_infix,         A_MAPACCESS,   0              },
	{ 105,    L_METHOD_REFERENCE, NULL,                  0,            ast_strkey_infix,         A_METHOD_REF,  0              },
	
	{ 110,    L_CURLY_OPEN,    ast_wrapping_expression,  A_MAP_INIT,   NULL,                     0,             L_CURLY_CLOSE  },
	{ 110,    L_PAREN_OPEN,    ast_wrapping_expression,  0,            ast_wrapping_empty_infix, A_CALL,        L_PAREN_CLOSE  },
	{ 110,    L_BRACKET_OPEN,  ast_wrapping_expression,  A_ARRAY_INIT, ast_wrapping_infix,       A_ARRACCESS,   L_BRACKET_CLOSE},

	/* prefix unary */
	{ 112,    L_SUB,           ast_simple_prefix,        A_MINUS,      NULL,                     0,             0              },
	{ 112,    L_NOT,           ast_simple_prefix,        A_NOT,        NULL,                     0,             0              },
	{ 112,    L_BIN_INV,       ast_simple_prefix,        A_BIN_INV,    NULL,                     0,             0              },
	{ 112,    L_INCR,          ast_simple_prefix,        A_INC,        ast_simple_postfix,       A_POST_INC,    0              },
	{ 112,    L_DECR,          ast_simple_prefix,        A_DEC,        ast_simple_postfix,       A_POST_DEC,    0              },
	
	/* C-like precedence (lower prio = binds tighter), bitwise before equality */
	{ 115,    L_MUL,           NULL,                     0,            ast_simple_infix,         A_MUL,         0              },
	{ 115,    L_DIV,           NULL,                     0,            ast_simple_infix,         A_DIV,         0              },
	{ 115,    L_MOD,           NULL,                     0,            ast_simple_infix,         A_MOD,         0              },

	{ 120,    L_ADD,           NULL,                     0,            ast_simple_infix,         A_ADD,         0              },
	{ 120,    L_SUB,           NULL,                     0,            ast_simple_infix,         A_SUB,         0              },

	{ 125,    L_BIN_LSHIFT,    NULL,                     0,            ast_simple_infix,         A_BIN_LSHIFT,  0              },
	{ 125,    L_BIN_RSHIFT,    NULL,                     0,            ast_simple_infix,         A_BIN_RSHIFT,  0              },

	{ 130,    L_GT,            NULL,                     0,            ast_simple_infix,         A_GT,          0              },
	{ 130,    L_LT,            NULL,                     0,            ast_simple_infix,         A_LT,          0              },
	{ 130,    L_GT_EQ,         NULL,                     0,            ast_simple_infix,         A_GT_EQ,       0              },
	{ 130,    L_LT_EQ,         NULL,                     0,            ast_simple_infix,         A_LT_EQ,       0              },

	{ 140,    L_BIN_AND,       NULL,                     0,            ast_simple_infix,         A_BIN_AND,     0              },
	{ 145,    L_BIN_XOR,       NULL,                     0,            ast_simple_infix,         A_BIN_XOR,     0              },
	{ 150,    L_BIN_OR,        NULL,                     0,            ast_simple_infix,         A_BIN_OR,      0              },

	{ 155,    L_EQ,            NULL,                     0,            ast_simple_infix,         A_EQ,          0              },
	{ 155,    L_NEQ,           NULL,                     0,            ast_simple_infix,         A_NEQ,         0              },

	{ 160,    L_AND,           NULL,                     0,            ast_simple_infix,         A_AND,         0              },
	{ 165,    L_OR,            NULL,                     0,            ast_simple_infix,         A_OR,          0              },
	{ 170,    L_NOTNULL,       NULL,                     0,            ast_simple_infix,         A_NOTNULL,     0              },

#define A_EXPR_PRIO 200

	{ 200,    L_FUNCTION,      ast_statement_function,   0,            NULL,                     0,             0              },

	// these shouldnt accept comma lists, thus are lower priority
	{ 250,    L_ADD_ASSIGN,   NULL,                     0,            ast_simple_infix,         A_ASSIGN_ADD,       0         },
	{ 250,    L_SUB_ASSIGN,   NULL,                     0,            ast_simple_infix,         A_ASSIGN_SUB,       0         },
	{ 250,    L_MUL_ASSIGN,   NULL,                     0,            ast_simple_infix,         A_ASSIGN_MUL,       0         },
	{ 250,    L_DIV_ASSIGN,   NULL,                     0,            ast_simple_infix,         A_ASSIGN_DIV,       0         },
	{ 250,    L_MOD_ASSIGN,   NULL,                     0,            ast_simple_infix,         A_ASSIGN_MOD,       0         },
	{ 250,    L_POW_ASSIGN,   NULL,                     0,            ast_simple_infix,         A_ASSIGN_POW,       0         },
	{ 250,    L_OR_ASSIGN,    NULL,                     0,            ast_simple_infix,         A_ASSIGN_OR,        0         },
	{ 250,    L_AND_ASSIGN,   NULL,                     0,            ast_simple_infix,         A_ASSIGN_AND,       0         },
	{ 250,    L_NOTNULL_ASSIGN, NULL,                   0,            ast_simple_infix,         A_ASSIGN_NOTNULL,   0         },
	{ 250,    L_BIN_OR_ASSIGN,  NULL,                   0,            ast_simple_infix,         A_ASSIGN_BIN_OR,    0         },
	{ 250,    L_BIN_AND_ASSIGN, NULL,                   0,            ast_simple_infix,         A_ASSIGN_BIN_AND,   0         },
	{ 250,    L_BIN_XOR_ASSIGN, NULL,                   0,            ast_simple_infix,         A_ASSIGN_BIN_XOR,   0         },
	{ 250,    L_LSHIFT_ASSIGN,  NULL,                   0,            ast_simple_infix,         A_ASSIGN_LSHIFT,    0         },
	{ 250,    L_RSHIFT_ASSIGN,  NULL,                   0,            ast_simple_infix,         A_ASSIGN_RSHIFT,    0         },
	

	{ 300,    L_COMMA,         NULL,                     0,            ast_comma_list,           0,             0              },
	{ 300,    L_HASHCOMMA,     NULL,                     0,            ast_comma_list,           0,             0              },
	{ 300,    L_COLON,         NULL,                     0,            ast_comma_list,           0,             0              },
	
#define A_EXPR_LIST_PRIO 301
	
	{ 500,    L_IF,            ast_statement_if,         0,            NULL,                     0,             0              },
	{ 500,    L_WHILE,         ast_statement_while,      0,            NULL,                     0,             0              },
	{ 500,    L_DO,            ast_statement_do,         0,            NULL,                     0,             0              },
	{ 500,    L_FOR,           ast_statement_for,        0,            NULL,                     0,             0              },

	{ 500,    L_RETURN,        ast_statement_return,     A_RETURN,     NULL,                     0,             0              },
	
	{ 500,    L_VAR,           ast_simple_prefix,        A_VAR,        NULL,                     0,             0              },
	{ 500,    L_GOTO,          ast_simple_prefix,        A_GOTO,       NULL,                     0,             0              },
	
	{ 500,    L_ASSIGNMENT,    NULL,                     0,            ast_simple_infix,         A_ASSIGN,           0         },
	
#define A_STATEMENT_PRIO 1000
};




#define AS_OPS_PRIO_COUNT (sizeof(as_ops_prio) / sizeof(as_ops_prio[0]))

/* find prefix entry matching current lookahead token */
static const ast_op_entry *ast_find_prefix(ast *a, uint32_t max_prio) {
	b_tok peek = b_parser_peek(a->p);
	if (!peek.data) return NULL;

	for (uint32_t i = 0; i < AS_OPS_PRIO_COUNT; i++) {
		if (!as_ops_prio[i].prefix) continue;
		if (as_ops_prio[i].prio > max_prio) continue;
		if (peek.t == as_ops_prio[i].token_type)
			return &as_ops_prio[i];
	}
	return NULL;
}

/* find infix entry matching current lookahead token */
static const ast_op_entry *ast_find_infix(ast *a, uint32_t max_prio) {
	b_tok peek = b_parser_peek(a->p);
	if (!peek.data) return NULL;

	for (uint32_t i = 0; i < AS_OPS_PRIO_COUNT; i++) {
		if (!as_ops_prio[i].infix) continue;
		if (as_ops_prio[i].prio >= max_prio) continue;
		if (peek.t == as_ops_prio[i].token_type)
			return &as_ops_prio[i];
	}
	return NULL;
}

/* parse expression with binding power / priority */
static ast_node *ast_expr_bp(ast *a, uint32_t max_prio) {
	/* prefix — fast lookahead lookup */
	const ast_op_entry *pre = ast_find_prefix(a, max_prio);
	if (!pre) return NULL;

	if (ast_debug) {
		const char *tname = (pre->token_type < L_COUNT) ? token_names[pre->token_type] : "???";
		fprintf(stderr, "  >%u %-18s p", max_prio, tname);
	}
	ast_node *left = pre->prefix(a, pre);
	if (ast_debug)
		fprintf(stderr, " = %p\n", (void *)left);
	if (!left) return NULL;

	/* infix loop — keep consuming infix ops while they fit in our prio */
	for (;;) {
		const ast_op_entry *entry = ast_find_infix(a, max_prio);
		if (!entry) break;

		if (ast_debug) {
			const char *tname = (entry->token_type < L_COUNT) ? token_names[entry->token_type] : "???";
			fprintf(stderr, "  >%u %-18s i", max_prio, tname);
		}
		ast_node *combined = entry->infix(a, left, entry);
		if (ast_debug)
			fprintf(stderr, " = %p\n", (void *)combined);
		if (!combined) break;
		left = combined;
	}

	return left;
}

static ast_node *ast_consume_expression(ast *a) {
	return ast_expr_bp(a, A_EXPR_PRIO);
}

static ast_node *ast_consume_expression_list(ast *a) {
	return ast_expr_bp(a, A_EXPR_LIST_PRIO);
}

static ast_node *ast_consume_function_name(ast *a) {
	return ast_expr_bp(a, A_VALUE_FN_NAME);
}





/* ================================================================
 *  AST dump
 * ================================================================ */

static void ast_dump(ast_node *n, int indent) {
	if (!n) return;
	uint32_t kind = A_TYPE(n->type);

	/* print node name + value */
	printf("%*s", indent * 2, "");
	const char *name = (kind < A_COUNT) ? ast_kind_names[kind] : "?";
	switch (kind) {
	case A_NUMBER:
		if      (n->type & A_NUMBER_DOUBLE) printf("%s %g",                          name, n->num_double);
		else if (n->type & A_NUMBER_HEX)    printf("%s 0x%llx",    name, (unsigned long long)n->num_int);
		else if (n->type & A_NUMBER_BIN)    printf("%s 0b (=%llu)",name, (unsigned long long)n->num_int);
		else                                printf("%s %llu",      name, (unsigned long long)n->num_int);
		break;
	case A_STRING:
	case A_IDENTIFIER:
		printf("%s %.*s", name, (int)n->token.len, n->token.data);
		break;
	default:
		printf("%s", name);
		if (n->token.len) {
			printf(" %.*s", (int)n->token.len, n->token.data);
		}
		break;
	}
	if (n->type & A_LIST) {
		printf(" [%u]", ast_node_list_length(n));
	}
	printf("\n");

	/* recurse into children */
	if (n->type & A_LIST) {
		for (uint32_t i = 0; i < ast_node_list_length(n); i++) {
			ast_dump(ast_node_list(n, i), indent + 1);
		}
		return;
	}

	/* named fields for specific node types */
	switch (kind) {
	case A_FUNCTION: {
		#define DUMP_FIELD(label, node) do { \
			printf("%*s" label ": ", (indent+1)*2, ""); \
			if (node) { printf("\n"); ast_dump(node, indent+2); } \
			else      { printf("(null)\n"); } \
		} while(0)
		DUMP_FIELD("name",   n->op_function.name);
		DUMP_FIELD("args",   n->op_function.args);
		DUMP_FIELD("body",   n->op_function.body);
		#undef DUMP_FIELD
		return;
	}
	case A_IF:
		#define DUMP_FIELD(label, node) do { \
			printf("%*s" label ": ", (indent+1)*2, ""); \
			if (node) { printf("\n"); ast_dump(node, indent+2); } \
			else      { printf("(null)\n"); } \
		} while(0)
		DUMP_FIELD("cond",   n->op_if.condition);
		DUMP_FIELD("body",   n->op_if.body);
		DUMP_FIELD("else",   n->op_if.else_body);
		#undef DUMP_FIELD
		return;
	case A_FOR_LOOP:
	case A_LOOP:
		#define DUMP_FIELD(label, node) do { \
			printf("%*s" label ": ", (indent+1)*2, ""); \
			if (node) { printf("\n"); ast_dump(node, indent+2); } \
			else      { printf("(null)\n"); } \
		} while(0)
		DUMP_FIELD("init",   n->op_loop.init);
		DUMP_FIELD("cond",   n->op_loop.condition);
		DUMP_FIELD("step",   n->op_loop.step);
		DUMP_FIELD("body",   n->op_loop.body);
		#undef DUMP_FIELD
		return;
	}

	/* generic fixed-arg fallback */
	uint32_t cnt = A_ARG_CNT(n->type);
	for (uint32_t i = 0; i < cnt; i++) {
		if (n->args[i])
			ast_dump(n->args[i], indent + 1);
		else
			printf("%*s(null)\n", (indent+1)*2, "");
	}
}

/* ================================================================
 *  Capture dump
 * ================================================================ */

static void dump_captures(cma_state *s, const uint8_t *base) {
	uint32_t count = cma_cap_count(s);
	printf("%u tokens:\n", count);

	for (uint32_t i = 0; i < count; i++) {
		cma_capture *c = cma_cap_get(s, i);
		size_t id = cma_cap_id(c);

		size_t offset = (size_t)(c->start - base);
		size_t len    = cma_cap_len(c);

		const char *name = (id < L_COUNT) ? token_names[id] : "???";

		printf("  [%3u] %5zu  %-18s \"", i, offset, name);

		for (size_t j = 0; j < len && j < 60; j++) {
			uint8_t ch = c->start[j];
			if (ch == '\n')      printf("\\n");
			else if (ch == '\r') printf("\\r");
			else if (ch == '\t') printf("\\t");
			else if (ch == '"')  printf("\\\"");
			else if (ch == '\\') printf("\\\\");
			else if (ch < 0x20)  printf("\\x%02x", ch);
			else                 putchar(ch);
		}
		if (len > 60) printf("...");
		printf("\"\n");
	}
}

/* ================================================================
 *  Main
 * ================================================================ */

#ifdef STANDALONE_PARSER
int main(int argc, char **argv) {
	if (argc < 2) {
		const char msg[] = "usage: parser [-d] <source-file>...\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		return 1;
	}

	int argi = 1;
	if (argi < argc && strcmp(argv[argi], "-d") == 0) {
		ast_debug = 1;
		argi++;
	}

	ci_init();
	ci_str_register();
	ci_arr_register();

	for (int i = argi; i < argc; i++) {
		b_parser *p = b_parser_new();
		if (!b_parser_load_file(p, argv[i])) {
			fprintf(stderr, "error: cannot read '%s'\n", argv[i]);
			free(p);
			continue;
		}

		size_t len = ci_str_len(p->buf);
		printf("=== %s (%zu bytes) ===\n", argv[i], len);

		uint32_t count = 0;
		b_tok tok;
		while ((tok = b_parser_next(p)).data) {
			const char *name = (tok.t < L_COUNT) ? token_names[tok.t] : "???";
			uint32_t line = b_parser_line_of(p, tok.data);

			printf("  [%3u] L%-4u %-18s \"", count++, line, name);
			for (uint32_t j = 0; j < tok.len && j < 60; j++) {
				uint8_t ch = (uint8_t)tok.data[j];
				if (ch == '\n')      printf("\\n");
				else if (ch == '\r') printf("\\r");
				else if (ch == '\t') printf("\\t");
				else if (ch == '"')  printf("\\\"");
				else if (ch == '\\') printf("\\\\");
				else if (ch < 0x20)  printf("\\x%02x", ch);
				else                 putchar(ch);
			}
			if (tok.len > 60) printf("...");
			printf("\"\n");
		}
		printf("%u tokens\n", count);

		b_parser_pos(p) = 0;

		ast *a = ast_new(p);
		ast_node *expr = ast_codelist(a);
		printf("\n=== AST ===\n");
		if (expr) {
			ast_dump(expr, 0);
			ast_node_free(expr);
		} else {
			printf("(parse failed)\n");
		}
		ast_free(a);

		b_parser_free(p);
	}

	ci_shutdown();
	return 0;
}
#endif /* STANDALONE_PARSER */
