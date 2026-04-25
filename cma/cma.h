#ifndef CMA_H
#define CMA_H

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

// ---------------- debug ----------------

#ifdef CMA_DEBUG_ENABLED
#define CMA_DEBUG(...) printf(__VA_ARGS__)
#else
#define CMA_DEBUG(...) ((void)0)
#endif

// ---------------- types ----------------

typedef struct cma_state cma_state;
typedef struct cma_op    cma_op;

#define CMA_OP __attribute__((preserve_none)) int

typedef CMA_OP (*cma_match_fn)(cma_state *s, cma_op *op);

enum {
    CMA_STR,
    CMA_SET,
    CMA_AND,
    CMA_OR,
    CMA_NOT,
    CMA_REP,
    CMA_AHEAD,
    CMA_REF,
    CMA_ENDL,
    CMA_CAP,
    CMA_NOOP,
    CMA_EXEC,
};

enum {
    CMA_OWN = 1 << 0,  // unused for now
    CMA_STR_INSENSITIVE = 1 << 1,
    CMA_CAP_CB = 1 << 2,
	CMA_CAP_INT = 1 << 3,
	CMA_CAP_NAMED = 1 << 4,
};

#define CMA_INF 0xFFFFFFFF

// ---------------- status (for longjmp) ----------------

enum {
    CMA_STATUS_OK = 0,
    CMA_END_REACHED,
    CMA_EXCEPTION,
};

// ---------------- state flags ----------------

enum {
    CMA_END_IS_MISS = 1 << 0,  // treat end-of-input as miss, not longjmp
};

// ---------------- bitset ----------------

typedef uint32_t cma_bitword;

#define CMA_BITS_PER_WORD  (sizeof(cma_bitword) * 8)
#define CMA_BITSET_WORDS   (256 / CMA_BITS_PER_WORD)

#define CMA_BIT_SET(bits, ch)   ((bits)[(ch) / CMA_BITS_PER_WORD] |=  (1u << ((ch) % CMA_BITS_PER_WORD)))
#define CMA_BIT_CLR(bits, ch)   ((bits)[(ch) / CMA_BITS_PER_WORD] &= ~(1u << ((ch) % CMA_BITS_PER_WORD)))
#define CMA_BIT_TEST(bits, ch)  ((bits)[(ch) / CMA_BITS_PER_WORD] &   (1u << ((ch) % CMA_BITS_PER_WORD)))

// ---------------- opcodes ----------------

struct cma_op {
    cma_match_fn fn;
    uint16_t type;
    uint16_t flags;
    void    *opctx;
};

typedef struct {
    cma_op         base;
    const uint8_t *buf;
    size_t         len;
} cma_op_str;

typedef struct {
    cma_op      base;
    cma_bitword bits[CMA_BITSET_WORDS];
    uint8_t     simd_lo[16];   // pshufb LUT: rows 0-7 by column
    uint8_t     simd_hi[16];   // pshufb LUT: rows 8-15 by column
} cma_op_set;

typedef struct {
    cma_op   base;
    char    *name;
    size_t   seq_len;
    cma_op*  seq[];
} cma_op_seq;

typedef struct {
    cma_op  base;
    cma_op *pattern;
} cma_op_wrap;

typedef cma_op *(*cma_ref_fn)(cma_state *s, void *ctx);

typedef struct {
    cma_op     base;
    cma_ref_fn cb;
    void      *ctx;
} cma_op_ref;

typedef struct {
    cma_op   base;
    uint32_t min;
    uint32_t max;
    cma_op  *pattern;
} cma_op_rep;

// WARNING: CAPcb callbacks fire on match, but if a PARENT pattern
// later backtracks, the callback's side effects are NOT undone.
// The callback cannot know it was backtracked. Do not allocate or
// mutate external state unless the grammar guarantees no backtracking
// past this point (e.g., unambiguous by first char, like JSON).
typedef CMA_OP (*cma_cap_cb_fn)(cma_state *s, void *ctx, cma_op *op);

typedef struct {
    cma_op  base;
    cma_op *pattern;
    union {
        char *name;
        void *ctx;
		size_t id;
        cma_cap_cb_fn cb;
    };
} cma_op_cap;

typedef cma_op cma_op_exec;

// ---------------- captures ----------------

typedef struct {
    cma_op        *parent;    // the cma_op_cap that created this
    const uint8_t *start;
    const uint8_t *end;
    uint32_t       nested;    // number of nested captures inside this one
} cma_capture;

// ---------------- backtrack stack ----------------

typedef struct {
    const uint8_t *pos;
    uint32_t       cap_len;   // capture stack depth at push time
} cma_save;

// ---------------- state ----------------

struct cma_state {
    const uint8_t *str;
    size_t         length;
    const uint8_t *pos;
    const uint8_t *end;

    cma_save      *stack;
    uint32_t       stack_len;
    uint32_t       stack_cap;

    cma_capture   *caps;
    uint32_t       cap_len;
    uint32_t       cap_cap;

    const uint8_t *farthest;   // high-water mark: furthest pos where a miss occurred

    uint32_t       flags;
    int            status;
    jmp_buf        jumpout;
};

// ---------------- core dispatch ----------------

static inline cma_op *cma_init_exec(cma_op_exec *o, cma_match_fn cb, void *ctx) {
    o->fn    = cb;
    o->type  = CMA_EXEC;
    o->flags = 0;
    o->opctx = ctx;
    return o;
}

static inline int cma_call(cma_state *s, cma_op *op) {
    return op->fn(s, op);
}

// ---------------- return helpers ----------------

static inline int cma_ret_matched(cma_state *s) {
    (void)s;
    return 1;
}

static inline int cma_ret_missed(cma_state *s) {
    if (s->pos > s->farthest) s->farthest = s->pos;
    return 0;
}

static inline int cma_ret_end_reached(cma_state *s) {
    if (s->flags & CMA_END_IS_MISS) return 0;
    s->status = CMA_END_REACHED;
    longjmp(s->jumpout, 1);
    return 0; // unreachable
}

static inline int cma_ret_exception(cma_state *s) {
    s->status = CMA_EXCEPTION;
    longjmp(s->jumpout, 1);
    return 0; // unreachable
}

// ---------------- backtrack stack ops ----------------

static inline uint32_t cma_push(cma_state *s) {
    if (s->stack_len >= s->stack_cap) {
        s->stack_cap = s->stack_cap ? s->stack_cap * 2 : 32;
        s->stack = realloc(s->stack, s->stack_cap * sizeof(cma_save));
    }
    uint32_t sp = s->stack_len++;
    s->stack[sp].pos     = s->pos;
    s->stack[sp].cap_len = s->cap_len;
    return sp;
}

static inline void cma_backtrack(cma_state *s, uint32_t sp) {
    s->pos     = s->stack[sp].pos;
    s->cap_len = s->stack[sp].cap_len;
    s->stack_len = sp;
}

static inline void cma_commit(cma_state *s, uint32_t sp) {
    s->stack_len = sp;
}

// ---------------- capture stack ops ----------------

static inline uint32_t cma_cap_push(cma_state *s) {
    if (s->cap_len >= s->cap_cap) {
        s->cap_cap = s->cap_cap ? s->cap_cap * 2 : 16;
        s->caps = realloc(s->caps, s->cap_cap * sizeof(cma_capture));
    }
    return s->cap_len++;
}

// ---------------- capture accessors ----------------

static inline char *cma_op_name(cma_op *op) {
    if (op->type != CMA_AND && op->type != CMA_OR) return NULL;
    return ((cma_op_seq *)op)->name;
}

static inline const char *cma_op_pname(cma_op *op) {
    char *n = cma_op_name(op);
    return n ? n : "(unnamed)";
}

static inline uint32_t      cma_cap_count(cma_state *s)            { return s->cap_len; }
static inline cma_capture   *cma_cap_get(cma_state *s, uint32_t i) { return &s->caps[i]; }
static inline const uint8_t *cma_cap_start(cma_capture *c)         { return c->start; }
static inline const uint8_t *cma_cap_end(cma_capture *c)           { return c->end; }
static inline size_t         cma_cap_len(cma_capture *c)            { return c->end - c->start; }
static inline uint32_t      cma_cap_nested(cma_capture *c)         { return c->nested; }

static inline char *cma_cap_name(cma_capture *c) {
    cma_op_cap *op = (cma_op_cap *)c->parent;
    return (op->base.flags & CMA_CAP_NAMED) ? op->name : NULL;
}

static inline size_t cma_cap_id(cma_capture *c) {
    cma_op_cap *op = (cma_op_cap *)c->parent;
    return (op->base.flags & CMA_CAP_INT) ? op->id : 0;
}

static inline cma_capture *cma_cap_find(cma_state *s, const char *name) {
    for (uint32_t i = 0; i < s->cap_len; i++) {
        char *n = cma_cap_name(&s->caps[i]);
        if (n && strcmp(n, name) == 0) return &s->caps[i];
    }
    return NULL;
}

// iterate all captures: cma_cap_foreach(state, cap) { ... }
#define cma_cap_foreach(s, c) \
    for (cma_capture *c = (s)->caps, *_end = (s)->caps + (s)->cap_len; c < _end; c++)

// iterate with index: cma_cap_foreach_i(state, i, cap) { ... }
#define cma_cap_foreach_i(s, i, c) \
    for (uint32_t i = 0; i < (s)->cap_len && ((c = &(s)->caps[i]), 1); i++)

// ---------------- match functions ----------------


static inline cma_op *cma_ref_ptr(cma_state *s, void *ctx) {
    (void)s;
    return *(cma_op **)ctx;
}

CMA_OP cma_match_endl(cma_state *s, cma_op *op);
CMA_OP cma_match_str(cma_state *s, cma_op *op);
CMA_OP cma_match_set(cma_state *s, cma_op *op);
CMA_OP cma_match_and(cma_state *s, cma_op *op);
CMA_OP cma_match_or (cma_state *s, cma_op *op);
CMA_OP cma_match_not(cma_state *s, cma_op *op);
CMA_OP cma_match_rep(cma_state *s, cma_op *op);
CMA_OP cma_match_rep_set(cma_state *s, cma_op *op);
CMA_OP cma_match_rep_set_nosimd(cma_state *s, cma_op *op);
CMA_OP cma_match_ahead(cma_state *s, cma_op *op);
CMA_OP cma_match_cap(cma_state *s, cma_op *op);
CMA_OP cma_match_noop(cma_state *s, cma_op *op);

// ---------------- init helpers ----------------

cma_op *cma_init_str(cma_op_str *o, const char *str, uint16_t flags);
cma_op *cma_init_set(cma_op_set *o);
cma_op *cma_init_seq(cma_op_seq *o, uint16_t type, const char *name, size_t n, cma_op **ops);
cma_op *cma_init_not(cma_op_wrap *o, cma_op *pattern);
cma_op *cma_init_ahead(cma_op_wrap *o, cma_op *pattern);
cma_op *cma_init_rep(cma_op_rep *o, uint32_t min, uint32_t max, cma_op *pattern);
cma_op *cma_init_ref(cma_op_ref *o, cma_ref_fn cb, void *ctx);
cma_op *cma_init_cap(cma_op_cap *o, cma_op *pattern, uint16_t flags, void* name);
cma_op *cma_init_cap_cb(cma_op_cap *o, cma_op *pattern, cma_cap_cb_fn cb, void *ctx);

// ---------------- platform: static compound literals ----------------

#ifdef __clang__
// Clang doesn't support static compound literals even in C23 mode
#define CMA_ALLOC_STATIC(type) \
    ({ static type _s = {0}; (type*)&_s; })
#define CMA_ALLOC_STATIC_INIT(type, ...) \
    ({ static type _s = __VA_ARGS__; (type*)&_s; })
#define cma_seq_static_array(ops) \
    ({ static char _s[cma_seq_size(ops)] = {0}; (cma_op_seq*)_s; })
#else
#define CMA_ALLOC_STATIC(type) &((static type){0})
#define CMA_ALLOC_STATIC_INIT(type, ...) &((static type)__VA_ARGS__)
#define cma_seq_static_array(ops) (cma_op_seq*) &((static char[cma_seq_size(ops)]){0})
#endif

// ---------------- convenience macros ----------------


// S("hello") - case-sensitive string
#define S(str) cma_init_str(CMA_ALLOC_STATIC(cma_op_str), str, 0)

// Si("hello") - case-insensitive string
#define Si(str) cma_init_str(CMA_ALLOC_STATIC(cma_op_str), str, CMA_STR_INSENSITIVE)

// R("a-zA-Z0-9_") - character class from spec
#define R(spec) ({ \
	static cma_op_set _o = {0}; \
	if (!_o.base.fn) { \
		cma_init_set(&_o); \
		cma_set_fill_str(&_o, spec); \
	} \
	(cma_op *)&_o; \
})


#define cma_seq_size(ops) (sizeof(cma_op_seq) + sizeof(ops) + sizeof(cma_op *))

// A(p1, p2, p3) - match all in sequence
#define A(...) ({ \
    cma_op *_ops[] = {__VA_ARGS__}; \
    cma_init_seq(cma_seq_static_array(_ops), CMA_AND, NULL, sizeof(_ops) / sizeof(cma_op*), _ops); \
})

// O(p1, p2, p3) - ordered choice
#define O(...) ({ \
    cma_op *_ops[] = {__VA_ARGS__}; \
    cma_init_seq(cma_seq_static_array(_ops), CMA_OR, NULL, sizeof(_ops) / sizeof(cma_op*), _ops); \
})

// NAMED("name", p1, p2, ...) - named AND sequence
#define NAMED(name, ...) ({ \
    cma_op *_ops[] = {__VA_ARGS__}; \
    cma_init_seq(cma_seq_static_array(_ops), CMA_AND, name, sizeof(_ops) / sizeof(cma_op*), _ops); \
})

// NOT(p1, p2) - fails if sequence matches, consumes nothing
#define NOT(...) cma_init_not(CMA_ALLOC_STATIC(cma_op_wrap), A(__VA_ARGS__))

// AHEAD(p1, p2) - matches if sequence matches, consumes nothing
#define AHEAD(...) cma_init_ahead(CMA_ALLOC_STATIC(cma_op_wrap), A(__VA_ARGS__))

// ENDL() - matches end of input
#define ENDL() ((cma_op *)CMA_ALLOC_STATIC_INIT(cma_op, { cma_match_endl, CMA_ENDL, 0, NULL }))

// REF(&op) - lazy reference, resolved at match time via cma_ref_ptr
#define REF(ptr) ( \
    (void)sizeof((cma_op **)0 == (ptr)), \
    cma_init_ref(CMA_ALLOC_STATIC(cma_op_ref), cma_ref_ptr, (ptr)))

// CAP(...) - capture a sequence
#define CAP(...) cma_init_cap(CMA_ALLOC_STATIC(cma_op_cap), A(__VA_ARGS__), 0, NULL)

// CAPn(pat, name) - named capture
#define CAPn(pat, name) cma_init_cap(CMA_ALLOC_STATIC(cma_op_cap), pat, CMA_CAP_NAMED, (void*)name)
// CAPi integer tagged instead of name
#define CAPi(pat, id) cma_init_cap(CMA_ALLOC_STATIC(cma_op_cap), pat, CMA_CAP_INT, (void*)id)

// CAPcb(pat, cb, userctx) - capture with callback, userctx goes to opctx
#define CAPcb(pat, callback, uctx) cma_init_cap_cb(CMA_ALLOC_STATIC(cma_op_cap), pat, callback, uctx)

// REP(min, max, pattern)
#define REP(lo, hi, pat) cma_init_rep(CMA_ALLOC_STATIC(cma_op_rep), lo, hi, pat)
#define MIN(n, pat)      cma_init_rep(CMA_ALLOC_STATIC(cma_op_rep), n, CMA_INF, pat)
#define MAX(n, pat)      cma_init_rep(CMA_ALLOC_STATIC(cma_op_rep), 0, n, pat)

// MAYBE(...) - match 0 or 1 occurrence
#define MAYBE(...) cma_init_rep(CMA_ALLOC_STATIC(cma_op_rep), 0, 1, A(__VA_ARGS__))

// EXEC(cb, ctx) - call user match function directly; ctx available via op->opctx
#define EXEC(cb, ctx) cma_init_exec(CMA_ALLOC_STATIC(cma_op_exec), cb, ctx)

// NOOP() - always matches, consumes nothing
#define NOOP() ((cma_op *)CMA_ALLOC_STATIC_INIT(cma_op, { cma_match_noop, CMA_NOOP, 0, NULL }))

// ANY(n) - match exactly n arbitrary bytes
#define ANY(n) REP(n, n, CHR(0, 255))

// CTX(opcode, ctx) - set opctx on an opcode, returns the opcode
#define CTX(op, ctx) ({ cma_op *_op = (op); _op->opctx = (ctx); _op; })

// WS() - skip optional whitespace (space, tab, newline, cr)
#define WS() MIN(0, CHRS(' ', '\t', '\n', '\r'))

// ────────────────────────────────────────────────────────────────
// CHARACTER SET HELPERS (internal)
// ────────────────────────────────────────────────────────────────

void cma_set_fill_range(cma_op_set *o, uint8_t start, uint8_t end);
void cma_set_fill_str(cma_op_set *o, const char *spec);
void cma_set_fill_chrs(cma_op_set *o, const uint8_t *chars, size_t count);
void cma_set_invert(cma_op_set *o);

// ────────────────────────────────────────────────────────────────
// CHARACTER SET VARIANTS
// ────────────────────────────────────────────────────────────────

// CHR(from, to) - character range (e.g., CHR('a', 'z'))
#define CHR(from, to) ({ \
	static cma_op_set _o = {0}; \
	if (!_o.base.fn) { \
		cma_init_set(&_o); \
		cma_set_fill_range(&_o, from, to); \
	} \
	(cma_op *)&_o; \
})

// CHRS('a', 'b', '\n', ...) - character set from individual char codes
#define CHRS(...) ({ \
	static cma_op_set _o = {0}; \
	if (!_o.base.fn) { \
		cma_init_set(&_o); \
		cma_set_fill_chrs(&_o, (const uint8_t[]){__VA_ARGS__}, \
			sizeof((const uint8_t[]){__VA_ARGS__})); \
	} \
	(cma_op *)&_o; \
})

// notR(spec) - negated character class (e.g., notR("0-9"))
#define notR(spec) ({ \
	static cma_op_set _o = {0}; \
	if (!_o.base.fn) { \
		cma_init_set(&_o); \
		cma_set_fill_str(&_o, spec); \
		cma_set_invert(&_o); \
	} \
	(cma_op *)&_o; \
})

// notCHRS('"', '\\', ...) - negated individual chars
#define notCHRS(...) ({ \
	static cma_op_set _o = {0}; \
	if (!_o.base.fn) { \
		cma_init_set(&_o); \
		cma_set_fill_chrs(&_o, (const uint8_t[]){__VA_ARGS__}, \
			sizeof((const uint8_t[]){__VA_ARGS__})); \
		cma_set_invert(&_o); \
	} \
	(cma_op *)&_o; \
})

// notCHR(from, to) - negated range (e.g., notCHR('0', '9'))
#define notCHR(from, to) ({ \
	static cma_op_set _o = {0}; \
	if (!_o.base.fn) { \
		cma_init_set(&_o); \
		cma_set_fill_range(&_o, from, to); \
		cma_set_invert(&_o); \
	} \
	(cma_op *)&_o; \
})

// ---------------- top-level match ----------------

// lifecycle
void cma_init(cma_state *s, const char *input, size_t len);
int  cma_match(cma_state *s, cma_op *op);
void cma_free(cma_state *s);

// simple one-shot (no cleanup, for tests)
int cma_run(cma_op *op, const char *input, size_t len);

// ---------------- debug ----------------

void cma_dump(cma_op *op, int indent);
void cma_dump_captures(cma_state *s);

#endif
