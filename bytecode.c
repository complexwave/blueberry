/*
 * bytecode.c — Citrin bytecode emitter (intermediate opcodes)
 *
 * Build:
 *   make bytecode
 *
 * Usage:
 *   ./bytecode <source-file>
 */

#include "parser.c"

/* ================================================================
 *  Checked allocator
 * ================================================================ */

#define b_error(...) do { fprintf(stderr, "error: " __VA_ARGS__); fputc('\n', stderr); exit(1); } while(0)

static void *b_malloc(size_t size) {
	void *p = malloc(size);
	if (!p) {
		fprintf(stderr, "error: out of memory\n");
		exit(1);
	}
	return p;
}

/* ================================================================
 *  Opcodes
 * ================================================================ */

#define B_OPCODES(X) \
	X(LOADINT) \
	X(LOADSTR) \
	X(LOADNULL)\
	X(LOADTRUE)\
	X(LOADFALSE)\
	X(MOVE)    \
	X(HASHACCESS) \
	X(MAPACCESS) \
	X(METHODBIND) \
	X(HASHSTORE)  \
	X(ARRAYSTORE) \
	X(ARRACCESS)  \
	X(NEWMAP)     \
	X(NEWARRAY)   \
	X(CALL)       \
	X(ADD)     \
	X(SUB)     \
	X(MUL)     \
	X(DIV)     \
	X(MOD)     \
	X(POW)     \
	X(NEG)     \
	X(NOT)     \
	X(BIN_INV) \
	X(BIN_OR)  \
	X(BIN_AND) \
	X(BIN_XOR) \
	X(BIN_LSHIFT) \
	X(BIN_RSHIFT) \
	X(EQ)      \
	X(NEQ)     \
	X(GT)      \
	X(LT)      \
	X(GT_EQ)   \
	X(LT_EQ)   \
	X(NOTNULL) \
	X(RETURN)  \
	X(JMP)     \
	X(JMPF)    \
	X(JMPT)    \
	X(LABEL)   \
	X(LOADFN)  \
	X(ITERINIT) \
	X(ITERSTEP) \
	X(MOVETO) \
	X(MOVEFROM)

enum {
	B__INVALID = 0,
#define X_ENUM(name) B_##name,
	B_OPCODES(X_ENUM)
#undef X_ENUM
	B_OP_COUNT
};

static const char *b_op_names[B_OP_COUNT] = {
	[0] = "(invalid)",
#define X_STR(name) [B_##name] = #name,
	B_OPCODES(X_STR)
#undef X_STR
};

/* ================================================================
 *  Opcode encoding types
 * ================================================================ */

enum {
	B_ENC_RRR,    /* op dst, src1, src2 */
	
	// unused
	B_ENC_RRU8,   /* op dst, src1, imm8 */
	B_ENC_RU16,   /* op dst, imm16      */
	
	B_ENC_RRI,    /* op dst, src1, imm32 */
	B_ENC_RI,     /* op dst, imm32 */
	
	B_ENC_R,      /* op dst, src (prefix unary) */
	B_ENC_R0,     /* op dst (no source, e.g. LOADTRUE) */
	B_ENC_VAR,    /* op [ reg, reg, ... ] — variable length */
	B_ENC_VAR_STRID, /* variable, dont emit string ids to registers */
	B_ENC_DECIDE, /* deferred — encoder pass picks RRR/RRU8/RU16 */
	B_ENC_CALL,   /* CALL base, nargs, nrets */
};

/* ================================================================
 *  Register (boxed for renaming + deferred value)
 * ================================================================ */

typedef struct b_register b_register;
typedef b_register* b_reg;

enum {
	B_REG_TMP,     /* 0 — temporary register (renameable) */
	B_REG_REG,     /* 1 — named/declared register */
	B_REG_INT,     /* 2 — deferred integer value */
	B_REG_DOUBLE,  /* 3 — deferred double value */
	B_REG_STRING,  /* 4 — deferred string value */
	B_REG_GLOBAL,  /* 5 — global access / unknown variable */
	B_REG_BOOL,    /* 6 — deferred bool/null (0=null, 1=true, 2=false) */
	B_REG_LABEL,   /* 7 — label reference */
};

struct b_register {
	uint8_t  number;
	uint8_t  type;
	uint8_t  freed;
	uint8_t  renamed;
	uint32_t strlen;
	union {
		int64_t  integer;
		double   dbl;
		char    *string;
		char    *global;
		char    *label;
	} value;
	
	uint32_t interned_string_id;
	uint8_t* label_patch_addr;
};

static b_reg b_reg_new(uint8_t num, uint8_t type) {
	b_reg r = b_malloc(sizeof(b_register));
	memset(r, 0, sizeof(b_register));
	r->number = num;
	r->type = type;
	return r;
}

/* allocate a value register — no register number consumed yet */
static b_reg b_reg_value(void) {
	b_reg r = b_malloc(sizeof(b_register));
	memset(r, 0, sizeof(b_register));
	return r;
}

static int b_reg_is_tmp(b_reg r) { return r->type == B_REG_TMP; }
static int b_reg_is_reg(b_reg r) { return r->type == B_REG_REG; }


static b_reg b_reg_imm(int64_t val) {
	b_reg r = b_reg_value();
	r->type = B_REG_INT;
	r->value.integer = val;
	return r;
}

/* check if value register holds an integer that fits uint8 */
static int b_reg_is_i8(b_reg r) {
	return r->type == B_REG_INT
		&& r->value.integer >= 0
		&& r->value.integer <= 255;
}

/* check if value register holds an integer that fits uint16 */
static int b_reg_is_i16(b_reg r) {
	return r->type == B_REG_INT
		&& r->value.integer >= 0
		&& r->value.integer <= UINT16_MAX;
}

static int b_reg_is_i32(b_reg r) {
	return r->type == B_REG_INT
		&& r->value.integer >= INT32_MIN
		&& r->value.integer <= INT32_MAX;
}


/* extract uint8 from value register */
static uint8_t b_reg_i8(b_reg r) {
	return (uint8_t)r->value.integer;
}

/* extract uint16 from value register */
static uint16_t b_reg_i16(b_reg r) {
	return (uint16_t)r->value.integer;
}

/* extract uint16 from value register */
static int32_t b_reg_i32(b_reg r) {
	if (!b_reg_is_i32(r)){
		b_error("b_reg_i32 on non i32 reg called");
	}
	
	return (int32_t)r->value.integer;
}

static int b_reg_same(b_reg a, b_reg b) {
	if(! (a && b)) return 0;
	
	return (a->type == B_REG_REG) && (b->type == B_REG_REG) &&
	(a->number == b->number);
}

/* ================================================================
 *  Opcode (intermediate — not final bytes)
 * ================================================================ */

typedef struct b_opcode b_opcode;

struct b_opcode {
	uint8_t  op;
	uint8_t  enc;
	union {
		struct {
			b_reg dst;
			b_reg src1;
			b_reg src2;
		} rrr;
		struct {
			b_reg   dst;
			b_reg   src1;
			uint8_t imm;
		} rru8;
		struct {
			b_reg    dst;
			uint16_t imm;
		} ru16;
		
		struct {
			b_reg    dst;
			b_reg   src1;
			uint16_t imm;
		} rri32;
		
		struct {
			b_reg dst;
			b_reg src;
		} r;
		struct {
			ci_array *regs;
			ci_array *rets;
		} var;
		struct {
			b_reg dst;
			b_reg src1;
			b_reg src2;
		} decide;
		struct {
			b_reg    base;
			b_reg*   all_regs;
			uint16_t nargs;
			uint16_t nrets;
		} call;
	};
};

/* ================================================================
 *  Unit + Codeblock
 * ================================================================ */

typedef struct b_unit      b_unit;
typedef struct b_function  b_function;
typedef struct b_codeblock b_codeblock;

struct b_function {
	b_unit      *unit;
	uint32_t     id;
	char        *name;
	ci_array    *bytecode;    /* b_opcode* */
	b_codeblock *cb;          /* root codeblock */
	uint32_t     label_next;
	uint8_t      arg_count;
};

struct b_unit {
	ci_map   *str_map;    /* string → (idx+1) as ci_ptr — O(1) intern lookup */
	ci_array *str_pool;   /* ordered strings for file writing, index = LOADSTR imm */
	ci_array *functions;  /* b_function* */
	uint32_t  anon_next;  /* counter for anonymous function names */
};

struct b_codeblock {
	b_function  *func;
	b_codeblock *parent;   /* scope chain for variable lookup */
	ci_map      *locals;
	uint8_t      reg_next;
	uint8_t      free_count;
	uint8_t      free_list[256];
	uint32_t     current_loop_id;  /* 0 if not in loop, otherwise unique loop ID */
};

static b_reg b_reg_reg(b_codeblock *cb, b_reg r);
static void b_reg_release(b_codeblock *cb, b_reg r);

static b_unit *b_unit_new(void) {
	b_unit *u = b_malloc(sizeof(b_unit));
	u->str_map  = ci_map_new(16);
	u->str_pool = ci_arr_new(8);
	u->functions = ci_arr_new(8);
	u->anon_next = 0;

	/* reserve string ID 0 as unused by storing empty string */
	ci_str *empty = ci_str_new(0);
	if (!empty)
		b_error("b_unit_new: out of memory");
	ci_arr_push(u->str_pool, (ci_ptr)empty);
	ci_map_put(u->str_map, empty, (ci_ptr)(uintptr_t)0);
	ci_nocnt(empty);

	return u;
}

static uint16_t b_unit_intern_str(b_unit *u, const char *s, uint32_t len) {
	/* check if already in pool by comparing content */
	uint32_t pool_len = ci_arr_len(u->str_pool);
	for (uint32_t i = 0; i < pool_len; i++) {
		ci_str *existing = (ci_str *)ci_arr_index(u->str_pool, i);
		if (ci_str_len(existing) == len &&
		    memcmp(ci_str_head(existing), s, len) == 0)
			return (uint16_t)i;
	}

	/* create new ci_string and add to pool */
	ci_str *tmp = ci_str_new(len);
	if (!tmp)
		b_error("intern_str: out of memory");
	ci_str_append(tmp, s, len);

	uint16_t idx = (uint16_t)ci_arr_len(u->str_pool);
	ci_arr_push(u->str_pool, (ci_ptr)tmp);
	ci_map_put(u->str_map, tmp, (ci_ptr)(uintptr_t)idx);
	ci_nocnt(tmp);
	return idx;
}

static b_function *b_function_new(b_unit *u, char *name) {
	b_function *f = b_malloc(sizeof(b_function));
	f->unit       = u;
	f->id         = ci_arr_len(u->functions);
	f->name       = name;
	f->bytecode   = ci_arr_new(64);
	f->cb         = NULL;
	f->label_next = 0;
	ci_arr_push(u->functions, (ci_ptr)f);
	return f;
}

static b_reg b_codeblock_declare(b_codeblock *cb, const char *name, uint32_t len);

static uint32_t b_loop_id_next = 1;  /* loop IDs start from 1 */

static uint32_t b_get_current_loop_id(b_codeblock *cb) {
	while (cb) {
		if (cb->current_loop_id != 0) {
			return cb->current_loop_id;
		}
		cb = cb->parent;
	}
	return 0;
}

static b_codeblock *b_codeblock_new(b_function *func, b_codeblock *parent) {
	b_codeblock *cb = b_malloc(sizeof(b_codeblock));
	cb->func       = func;
	cb->parent     = parent;
	cb->locals     = ci_map_new(8);
	cb->reg_next   = parent ? parent->reg_next : 0;
	cb->free_count = 0;
	cb->current_loop_id = 0;
	if (!parent) {
		b_codeblock_declare(cb, "self", 4);
		/* add _global as a reserved local pointing to register 255 */
		b_reg global_reg = b_reg_new(255, B_REG_REG);
		char *key = b_malloc(8);
		memcpy(key, "_global", 8);
		ci_map_set_str(cb->locals, key, (ci_ptr)global_reg);
	}
	return cb;
}

static b_reg b_reg_alloc(b_codeblock *cb) {
	if (cb->reg_next == 255)
		b_error("register overflow");
	return b_reg_new(cb->reg_next++, B_REG_REG);
}

static uint32_t b_reg_tmp__alloc_number(b_codeblock *cb) {
	if (cb->free_count > 0){
		return cb->free_list[--cb->free_count];
	}
	
	if (cb->reg_next == 254)
		b_error("register overflow");
	
	return cb->reg_next++;
}

int freelist_compare(const void* a, const void* b) {
   return (  ((int32_t) *(uint8_t*)a) - ((int32_t) *(uint8_t*)b)  );
}

static void b_reg_sort_freelist(b_codeblock *cb) {
	qsort(cb->free_list, cb->free_count, sizeof(uint8_t), freelist_compare);
}

static b_reg b_reg_tmp(b_codeblock *cb) {
	return b_reg_new(b_reg_tmp__alloc_number(cb), B_REG_TMP);
}

static b_reg b_reg_tmp_fresh(b_codeblock *cb) {
	if (cb->reg_next == 254)
		b_error("register overflow");
	
	return b_reg_new( cb->reg_next++ , B_REG_TMP);
}

static void b_reg_tmp_continuous(b_codeblock *cb, b_reg* dst, int32_t registers_required) {
	b_reg_sort_freelist(cb);

	int32_t end = cb->free_count - registers_required;

	if(end < 0) goto alloc_new;

	int32_t pos = cb->free_count-1;
	while(pos > end){
		if( (cb->free_list[pos]-1) != cb->free_list[pos-1]) goto alloc_new;
		pos--;
	}

	while(end < cb->free_count){
		*dst = b_reg_new( cb->free_list[end] , B_REG_TMP);

		end++;
		dst++;
	}

	cb->free_count -= registers_required;
	
	return;

	alloc_new:

	while(registers_required--){
		*dst = b_reg_tmp_fresh(cb);
		dst++;
	}
}


static void b_reg_tmp_release_continuous(b_codeblock *cb, b_reg* dst, int32_t regs) {
	while(regs--){
		b_reg_release(cb, *dst);
		dst++;
	}
}

static void b_reg_free(b_codeblock *cb, b_reg r) {
	if(r->freed){
		//b_error("b_reg_free: register double free");
		return;
	}
	
	if (!b_reg_is_tmp(r))
		b_error("b_reg_free: not a tmp (type=%u)", r->type);
	
	cb->free_list[cb->free_count++] = r->number;
	
	r->freed = 1;
}

/* mark register as no longer needed by the calling opcode.
 * frees tmp registers back to the freelist; locals are left alone. */
static void b_reg_release(b_codeblock *cb, b_reg r) {
	if (b_reg_is_tmp(r))
		b_reg_free(cb, r);
}


static void b_reg_no_rename(b_codeblock *cb, b_reg reg) {
	reg->renamed = 1;
}

static int b_reg_rename(b_codeblock *cb, b_reg src, b_reg dst) {
	if (!b_reg_is_tmp(src) || src->renamed)
		return 0;

	b_reg_free(cb, src);

	src->number = dst->number;
	src->type = dst->type;
	src->renamed = 1;
	return 1;
}

static b_reg b_codeblock_find(b_codeblock *cb, const char *name);

static b_codeblock *b_codeblock_root(b_codeblock *cb) {
	while (cb->parent)
		cb = cb->parent;
	return cb;
}

static b_reg b_reg_label(b_codeblock *cb, const char *name, size_t len) {
	b_reg existing = b_codeblock_find(cb, name);
	if (existing) {
		if (existing->type != B_REG_LABEL)
			b_error("'%s' already declared as variable", name);
		return existing;
	}

	b_reg r = b_reg_value();
	r->type = B_REG_LABEL;
	r->number = 0;
	
	r->strlen = len;
	if(!len){
		r->strlen = (uint32_t)strlen(name);
	}
	
	char *key = b_malloc(r->strlen + 1);
	memcpy(key, name, r->strlen);
	key[r->strlen] = 0;
	
	r->value.label = (char *)key;
	
	ci_map_set_str(cb->locals, key, (ci_ptr)r);

	return r;
}

static b_reg b_ulabel_idx(b_codeblock *cb, const char *prefix, uint32_t idx) {
	char name[256];
	snprintf(name, sizeof(name), "%s_%u", prefix, idx);

	b_reg r = b_reg_value();
	r->type = B_REG_LABEL;
	r->strlen = (uint32_t)strlen(name);
	r->value.label = b_malloc(r->strlen + 1);
	memcpy(r->value.label, name, r->strlen + 1);

	char *key = b_malloc(r->strlen + 1);
	memcpy(key, name, r->strlen + 1);
	ci_map_set_str(cb->locals, key, (ci_ptr)r);

	return r;
}

static b_reg b_reg_ulabel(b_codeblock *cb, const char *prefix) {
	uint32_t id = cb->func->label_next++;
	return b_ulabel_idx(cb, prefix, id);
}

/* find identifier in this codeblock's locals (linear scan, strcmp) */
static b_reg b_codeblock_find(b_codeblock *cb, const char *name) {
	return (b_reg)ci_map_get_str(cb->locals, name);
}

static void _dump_locals(b_codeblock *cb) {
	int depth = 0;
	for (b_codeblock *scope = cb; scope; scope = scope->parent, depth++) {
		ci_map_kv *kvs = (ci_map_kv *)scope->locals->space;
		uint32_t buckets = scope->locals->divmask + 1;
		fprintf(stderr, "%s[%d]:", depth == 0 ? "current" : "parent", depth);
		int found = 0;
		for (uint32_t i = 0; i < buckets; i++) {
			if (kvs[i].key) {
				b_reg r = (b_reg)kvs[i].val;
				fprintf(stderr, " %s=%d", (char *)kvs[i].key, r->number);
				found = 1;
			}
		}
		if (!found) fprintf(stderr, " (empty)");
		fprintf(stderr, "\n");
	}
}

/* look up identifier walking scope chain; returns B_REG_GLOBAL if not found */
static b_reg b_codeblock_get_ident(b_codeblock *cb, const char *name, uint32_t len) {
	char buf[256];
	if (len > 255) len = 255;
	memcpy(buf, name, len);
	buf[len] = '\0';

	for (b_codeblock *scope = cb; scope; scope = scope->parent) {
		b_reg r = b_codeblock_find(scope, buf);
		if (r) return r;
	}

	return NULL;
}

/* declare a local with a specific register */
static b_reg b_codeblock_declare_reg(b_codeblock *cb, const char *name, uint32_t len, b_reg r) {
	if (r->type == B_REG_TMP)
		r->type = B_REG_REG;
	else if (r->type != B_REG_REG)
		b_error("declare_reg: expected tmp or reg, got type %u", r->type);

	char *key = b_malloc(len + 1);
	memcpy(key, name, len);
	key[len] = '\0';

	b_reg existing = b_codeblock_find(cb, key);
	if (existing) {
		free(key);
		return existing;
	}

	ci_map_set_str(cb->locals, key, (ci_ptr)r);
	return r;
}

/* declare a new local — strdup the name so it's null-terminated */
static b_reg b_codeblock_declare(b_codeblock *cb, const char *name, uint32_t len) {
	return b_codeblock_declare_reg(cb, name, len, b_reg_alloc(cb));
}


/* ================================================================
 *  Emit helpers — create opcode and push to unit
 * ================================================================ */

static void b_emit(b_codeblock *cb, b_opcode *op) {
	ci_arr_push(cb->func->bytecode, (ci_ptr)op);
}

static b_opcode *b_opcode_new(void) {
	b_opcode *op = b_malloc(sizeof(b_opcode));
	memset(op, 0, sizeof(b_opcode));
	return op;
}

static void b_emit_rrr(b_codeblock *cb, uint8_t opnum, b_reg dst, b_reg src1, b_reg src2) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_RRR;

	op->rrr.dst  = dst;
	op->rrr.src1 = src1;
	op->rrr.src2 = src2;

	b_emit(cb, op);
}

static void b_emit_rru8(b_codeblock *cb, uint8_t opnum, b_reg dst, b_reg src1, uint8_t imm) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_RRU8;

	op->rru8.dst  = dst;
	op->rru8.src1 = src1;
	op->rru8.imm  = imm;

	b_emit(cb, op);
}

static void b_emit_ru16(b_codeblock *cb, uint8_t opnum, b_reg dst, uint16_t imm) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_RU16;

	op->ru16.dst = dst;
	op->ru16.imm = imm;

	b_emit(cb, op);
}

static void b_emit_rri32(b_codeblock *cb, uint8_t opnum, b_reg dst, b_reg src1, int32_t imm) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_RRI;

	op->rri32.dst = dst;
	op->rri32.src1 = src1;
	op->rri32.imm = imm;

	b_emit(cb, op);
}

static void b_emit_ri32(b_codeblock *cb, uint8_t opnum, b_reg dst, int32_t imm) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_RI;

	op->rri32.dst = dst;
	op->rri32.src1 = NULL;
	op->rri32.imm = imm;

	b_emit(cb, op);
}

static void b_emit_call_op(b_codeblock *cb, b_reg* base, uint16_t nargs, uint16_t nrets) {
	b_opcode *op = b_opcode_new();
	op->op  = B_CALL;
	op->enc = B_ENC_CALL;
	op->call.base  = base[0];
	op->call.all_regs  = base;
	op->call.nargs = nargs;
	op->call.nrets = nrets;
	
	b_emit(cb, op);
}

static void b_emit_r(b_codeblock *cb, uint8_t opnum, b_reg dst, b_reg src) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_R;

	op->r.dst = dst;
	op->r.src = src;

	b_emit(cb, op);
	
	if (src != dst)
		b_reg_release(cb, src);
}


static b_opcode *b_new_var(uint8_t opnum) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_VAR;
	op->var.regs = ci_arr_new(8);
	op->var.rets = NULL;
	
	return op;
}

static void b_op_release_regs(b_codeblock *cb, b_opcode *op) {
	if (op->enc != B_ENC_VAR)
		return;
	uint32_t cnt = ci_arr_len(op->var.regs);
	/* skip slot 0 — it's the dst */
	for (uint32_t i = 1; i < cnt; i++) {
		b_reg r = (b_reg)ci_arr_index(op->var.regs, i);
		b_reg_release(cb, r);
	}
}

static void b_op_release_rets(b_codeblock *cb, b_opcode *op) {
	if (op->enc != B_ENC_VAR || !op->var.rets)
		return;
	uint32_t cnt = ci_arr_len(op->var.rets);
	for (uint32_t i = 0; i < cnt; i++) {
		b_reg r = (b_reg)ci_arr_index(op->var.rets, i);
		b_reg_release(cb, r);
	}
}

static void b_emit_var(b_codeblock *cb, b_opcode *op) {
	b_emit(cb, op);
	b_op_release_regs(cb, op);
}

static void b_op_pushret(b_opcode *op, b_reg r) {
	if (!op->var.rets)
		op->var.rets = ci_arr_new(8);
	ci_arr_push(op->var.rets, (ci_ptr)r);
}

static void b_op_pushreg(b_opcode *op, b_reg r) {
	ci_arr_push(op->var.regs, (ci_ptr)r);
}


static void b_emit_decide(b_codeblock *cb, b_opcode *op) {
	if(op->enc != B_ENC_DECIDE){
		b_error("b_emit_decide on non decide op");
	}
	
	b_reg dst  = op->decide.dst;
	b_reg src1 = op->decide.src1;
	b_reg src2 = op->decide.src2;

	b_reg_reg(cb, src1);
	
	if (b_reg_is_i32(src2)) {
		op->rri32.dst  = dst;
		
		if(b_reg_same(dst, src1)){
			op->enc = B_ENC_RI;
			op->rri32.src1 = NULL;
		} else {
			op->enc = B_ENC_RRI;
			op->rri32.src1 = src1;
		}
		
		op->rri32.imm = b_reg_i32(src2);
		b_emit(cb, op);
		
		return;
	}

	/* fallback: RRR — resolve both, LOADINTs emitted before this op */
	b_reg_reg(cb, src2);
	op->enc = B_ENC_RRR;
	op->rrr.dst  = dst;
	op->rrr.src1 = src1;
	op->rrr.src2 = src2;
	b_emit(cb, op);
}

/* deferred encoding — stores raw reg objects, encoder pass decides format */
static void b_emit_any(b_codeblock *cb, uint8_t opnum, b_reg dst, b_reg src1, b_reg src2) {
	b_opcode *op = b_opcode_new();
	op->op  = opnum;
	op->enc = B_ENC_DECIDE;

	op->decide.dst  = dst;
	op->decide.src1 = src1;
	op->decide.src2 = src2;

	b_emit_decide(cb, op);
}

static void b_emit_label(b_codeblock *cb, b_reg label) {
	b_opcode *op = b_opcode_new();
	op->op  = B_LABEL;
	op->enc = B_ENC_R0;
	op->r.dst = label;
	b_emit(cb, op);
}

static void b_emit_jmp(b_codeblock *cb, b_reg label) {
	b_opcode *op = b_opcode_new();
	op->op  = B_JMP;
	op->enc = B_ENC_R0;
	op->r.dst = label;
	b_emit(cb, op);
}

static void b_emit_jmpf_nr(b_codeblock *cb, b_reg cond, b_reg label) {
	b_opcode *op = b_opcode_new();
	op->op  = B_JMPF;
	op->enc = B_ENC_R;
	op->r.dst = cond;
	op->r.src = label;
	b_emit(cb, op);
}

static void b_emit_jmpt_nr(b_codeblock *cb, b_reg cond, b_reg label) {
	b_opcode *op = b_opcode_new();
	op->op  = B_JMPT;
	op->enc = B_ENC_R;
	op->r.dst = cond;
	op->r.src = label;
	b_emit(cb, op);
}

static void b_emit_jmpf(b_codeblock *cb, b_reg cond, b_reg label) {
	b_emit_jmpf_nr(cb, cond, label);
	b_reg_release(cb, cond);
}

static void b_emit_jmpt(b_codeblock *cb, b_reg cond, b_reg label) {
	b_emit_jmpt_nr(cb, cond, label);
	b_reg_release(cb, cond);
}

/* ================================================================
 *  Value resolution — materialize deferred value into a real register
 * ================================================================ */

/* resolve value register to a materialized register (idempotent).
 * emits use r directly so rename propagates through opcodes. */
static b_reg b_reg_reg(b_codeblock *cb, b_reg r) {
	if (r->type == B_REG_TMP || r->type == B_REG_REG) {
		return r;
	}

	uint8_t old_type = r->type;
	int64_t old_int = r->value.integer;

	/* convert r in-place before emitting so the opcode stores r */
	r->type = B_REG_TMP;
	r->number = b_reg_tmp__alloc_number(cb);

	switch (old_type) {
	case B_REG_INT:
		if (old_int < INT32_MIN || old_int > INT32_MAX)
			b_error("integer %lld exceeds u32 range", (long long)old_int);
		
		b_emit_ri32(cb, B_LOADINT, r, old_int);
		break;

	case B_REG_BOOL: {
		static const uint8_t bool_ops[] = {
			[0] = B_LOADNULL,
			[1] = B_LOADTRUE,
			[2] = B_LOADFALSE,
		};
		b_opcode *bop = b_opcode_new();
		bop->op = bool_ops[old_int];
		bop->enc = B_ENC_R0;
		bop->r.dst = r;
		b_emit(cb, bop);
		break;
	}

	case B_REG_DOUBLE:
		b_error("float literals not yet supported");

	case B_REG_STRING:
		b_emit_ri32(cb, B_LOADSTR, r, r->interned_string_id);
		break;

	default:
		b_error("unknown register type %u", old_type);
	}

	return r;
}

/* ================================================================
 *  Encoder pass — resolve B_ENC_DECIDE opcodes
 * ================================================================ */

static void b_encode(b_codeblock *cb) {
	ci_array *old = cb->func->bytecode;
	ci_array *out = ci_arr_new(ci_arr_len(old) * 2);
	uint32_t count = ci_arr_len(old);

	/* temporarily swap so b_reg_reg emits into the new array */
	cb->func->bytecode = out;

	for (uint32_t i = 0; i < count; i++) {
		b_opcode *op = (b_opcode *)ci_arr_index(old, i);

		if (op->enc == B_ENC_VAR || op->enc == B_ENC_VAR_STRID ) {
			/* resolve any deferred values in the reg list */
			uint32_t vcnt = ci_arr_len(op->var.regs);
			for (uint32_t v = 0; v < vcnt; v++) {
				b_reg r = (b_reg)ci_arr_index(op->var.regs, v);

				/* skip B_REG_STRING values for VAR_STRID encoding */
				if (op->enc == B_ENC_VAR_STRID && r->type == B_REG_STRING)
					continue;

				b_reg_reg(cb, r);
			}
			ci_arr_push(out, (ci_ptr)op);
			continue;
		}

		if (op->enc != B_ENC_DECIDE) {
			ci_arr_push(out, (ci_ptr)op);
			continue;
		}
		
		b_emit_decide(cb, op);
	}

	cb->func->bytecode = out;
	/* old array leaked — will fix later */
}

/* ================================================================
 *  AST codegen dispatch
 * ================================================================ */

typedef b_reg (*b_emit_fn)(b_codeblock *cb, ast_node *n, uint32_t ctx);

typedef struct {
	uint32_t   ast_type;
	b_emit_fn  emit;
	uint32_t   ctx;
} b_dispatch_entry;

/* forward */
static b_reg b_consume_ast(b_codeblock *cb, ast_node *n);
static void b_consume_codelist(b_codeblock *cb, ast_node *block);

/* ---- emit functions ---- */

static b_reg b_emit_number_value(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)cb;
	(void)ctx;

	b_reg dst = b_reg_value();

	if (n->type & A_NUMBER_DOUBLE) {
		dst->type = B_REG_DOUBLE;
		dst->value.dbl = n->num_double;
	} else {
		dst->type = B_REG_INT;
		dst->value.integer = (int64_t)n->num_int;
	}

	return dst;
}

static b_reg b_emit_simple_infix(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	b_reg src1 = b_consume_ast(cb, n->args[0]);
	b_reg src2 = b_consume_ast(cb, n->args[1]);

	b_reg dst = b_reg_tmp(cb);
	b_emit_any(cb, (uint8_t)ctx, dst, src1, src2);
	
	b_reg_release(cb, src1);
	b_reg_release(cb, src2);

	return dst;
}

static b_reg b_emit_prefix(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	b_reg src = b_consume_ast(cb, n->args[0]);

	b_reg dst = b_reg_tmp(cb);
	b_emit_r(cb, (uint8_t)ctx, dst, b_reg_reg(cb, src));
	b_reg_release(cb, src);
	
	return dst;
}

/* Process C-style escape sequences in a string literal.
 * src/len: raw token content (between the quotes, no NUL terminator needed).
 * dst: output buffer, must be at least len bytes (unescaped is always <=).
 * Returns the unescaped byte count. */
static uint32_t b_unescape_str(const char *src, uint32_t len, char *dst) {
	uint32_t r = 0, w = 0;
	while (r < len) {
		if (src[r] != '\\' || r + 1 >= len) {
			dst[w++] = src[r++];
			continue;
		}
		r++; /* consume backslash */
		switch (src[r++]) {
		case 'n':  dst[w++] = '\n'; break;
		case 't':  dst[w++] = '\t'; break;
		case 'r':  dst[w++] = '\r'; break;
		case '\\': dst[w++] = '\\'; break;
		case '\'': dst[w++] = '\''; break;
		case '"':  dst[w++] = '"';  break;
		case '0':  dst[w++] = '\0'; break;
		case 'x': {
			/* \xHH — exactly two hex digits */
			#define HEX(c) ((c)>='0'&&(c)<='9' ? (c)-'0' : \
			                (c)>='a'&&(c)<='f' ? (c)-'a'+10 : \
			                (c)>='A'&&(c)<='F' ? (c)-'A'+10 : -1)
			if (r + 1 < len) {
				int hi = HEX(src[r]), lo = HEX(src[r + 1]);
				if (hi >= 0 && lo >= 0) {
					dst[w++] = (char)((hi << 4) | lo);
					r += 2;
					break;
				}
			}
			#undef HEX
			/* not valid hex — emit literally */
			dst[w++] = '\\';
			dst[w++] = 'x';
			break;
		}
		default:
			/* unknown escape — keep backslash + char */
			dst[w++] = '\\';
			dst[w++] = src[r - 1];
			break;
		}
	}
	return w;
}

static b_reg b_emit_string_value(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;

	/* unescape into a fresh buffer; unescaped length <= raw length */
	char *buf = b_malloc(n->token.len + 1);
	uint32_t ulen = b_unescape_str(n->token.data, n->token.len, buf);

	uint16_t idx = b_unit_intern_str(cb->func->unit, buf, ulen);

	b_reg dst = b_reg_value();
	dst->type = B_REG_STRING;

	dst->strlen = ulen;
	dst->value.string = buf;

	dst->interned_string_id = idx;

	return dst;
}


static b_reg b_emit_hashaccess(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;
	uint32_t cnt = ast_node_list_length(n);
	b_reg dst = b_consume_ast(cb, ast_node_list(n, 0));
	b_reg map = NULL;

	b_opcode *op = NULL;

	for (uint32_t i = 1; i < cnt; i++) {
		b_reg key = b_consume_ast(cb, ast_node_list(n, i));
			
		if(key->type == B_REG_STRING){
			if (!op){
				map = dst;
				dst = b_reg_tmp(cb);
				op = b_new_var(B_HASHACCESS);
				op->enc = B_ENC_VAR_STRID;
				
				/* dst, obj, key0, key1, ... */
				b_op_pushreg(op, dst);
				b_op_pushreg(op, b_reg_reg(cb, map));
				b_reg_release(cb, map);
			}
			
			b_op_pushreg(op, key);
		} else {
			// split hashaccess
			if (op){
				b_emit_var(cb, op);
			}

			op = NULL;

			map = dst;
			dst = b_reg_tmp(cb);
			b_emit_rrr(cb, B_HASHACCESS, dst, b_reg_reg(cb, map), b_reg_reg(cb, key));
			b_reg_release(cb, map);
			b_reg_release(cb, key);
			
		}
	}

	if (op){
		b_emit_var(cb, op);
		b_reg_release(cb, map);
	}

	return dst;
}

/* emit hashaccess split at last key, returning fn reg and self reg.
 * obj.method       → self=obj,       fn=HASHACCESS(obj,"method")
 * obj.a.b.method   → self=obj.a.b,   fn=HASHACCESS(self,"method")
 * pops last key from AST, calls b_emit_hashaccess for the rest */
static b_reg b_emit_method_hashaccess(b_codeblock *cb, ast_node *n, b_reg *out_self) {
	uint32_t cnt = ast_node_list_length(n);
	/* simple case: obj.method — self is obj directly */
	if (cnt == 2) {
		*out_self = b_reg_reg(cb, b_consume_ast(cb, ast_node_list(n, 0)));
		b_reg key = b_consume_ast(cb, ast_node_list(n, 1));
		b_reg dst = b_reg_tmp(cb);
		
		b_emit_rrr(cb, B_HASHACCESS, dst, *out_self, b_reg_reg(cb, key));
		
		b_reg_release(cb, *out_self);
		b_reg_release(cb, key);
		
		return dst;
	}

	/* chain: pop last key, emit rest via b_emit_hashaccess to get self */
	ast_node *lastkey_node = (ast_node *)ci_arr_pop(n->nodes);
	*out_self = b_reg_reg(cb, b_emit_hashaccess(cb, n, 0));


	b_reg key = b_consume_ast(cb, lastkey_node);
	b_reg dst = b_reg_tmp(cb);
	
	b_emit_rrr(cb, B_HASHACCESS, dst, *out_self, b_reg_reg(cb, key));
	
	b_reg_release(cb, *out_self);
	b_reg_release(cb, key);
	
	return dst;
}

/* expr iteration — treats single node as length-1 list */
static uint32_t b_expr_cnt(ast_node *n) {
	if (A_TYPE(n->type) == A_EXPRLIST)
		return ast_node_list_length(n);
	return 1;
}

static ast_node *b_expr_idx(ast_node *n, uint32_t idx) {
	if (A_TYPE(n->type) == A_EXPRLIST)
		return ast_node_list(n, idx);
	return idx == 0 ? n : NULL;
}

static b_reg b_emit_map_init(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;

	b_reg dst = b_reg_tmp(cb);
	b_opcode *op = b_new_var(B_NEWMAP);
	b_op_pushreg(op, dst);

	/* n is flat key-value list: [key, val, key, val, ...] */
	uint32_t cnt = ast_node_list_length(n);
	if (cnt % 2 != 0)
		b_error("map init: need even number of key-value pairs");

	for (uint32_t i = 0; i < cnt; i += 2) {
		b_reg key = b_consume_ast(cb, ast_node_list(n, i));
		b_reg val = b_consume_ast(cb, ast_node_list(n, i + 1));

		b_op_pushreg(op, b_reg_reg(cb, val));
		b_op_pushreg(op, key);  /* keep key as B_REG_STRING for encoder */
	}

	op->enc = B_ENC_VAR_STRID;

	b_emit_var(cb, op);
	return dst;
}

static b_reg b_emit_array_init(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;

	b_reg dst = b_reg_tmp(cb);
	b_opcode *op = b_new_var(B_NEWARRAY);
	b_op_pushreg(op, dst);

	/* n is list of values */
	uint32_t cnt = ast_node_list_length(n);

	for (uint32_t i = 0; i < cnt; i++) {
		b_reg val = b_consume_ast(cb, ast_node_list(n, i));
		b_op_pushreg(op, b_reg_reg(cb, val));
	}

	op->enc = B_ENC_VAR;

	b_emit_var(cb, op);
	return dst;
}

static b_reg b_emit_arraccess(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;

	/* n is node with args[0]=array, args[1]=index */
	b_reg arr = b_reg_reg(cb, b_consume_ast(cb, n->args[0]));
	b_reg idx = b_reg_reg(cb, b_consume_ast(cb, n->args[1]));

	b_reg dst = b_reg_tmp(cb);
	
	b_emit_rrr(cb, B_ARRACCESS, dst, arr, idx);
	
	b_reg_release(cb, arr);
	b_reg_release(cb, idx);
	
	return dst;
}

/* ================================================================
 *  MOVETO / MOVEFROM — contiguous register gather/scatter
 * ================================================================
 *
 * Rename tmps directly into target slots where possible.
 * Trim renamed from head and tail — only emit instruction for
 * the middle portion that needs actual copies.
 * If all regs are renamed, emit nothing.
 *
 * MOVETO/MOVEFROM [base] [cnt] [wordcnt]
 *   [reg0, reg1, ..., regN-1]   (packed u8 per reg, padded to 4)
 */
static void b_emit_regmove(b_codeblock *cb, uint8_t optype, b_reg *base, b_reg *regs, uint32_t cnt) {
	if (cnt == 0)
		return;

	uint8_t renamed[256];
	for (uint32_t i = 0; i < cnt; i++)
		renamed[i] = b_reg_rename(cb, regs[i], base[i]);

	/* trim renamed from head */
	uint32_t head = 0;
	while (head < cnt && renamed[head])
		head++;

	/* trim renamed from tail */
	uint32_t tail = cnt;
	while (tail > head && renamed[tail - 1])
		tail--;

	if (head >= tail)
		return;

	b_opcode *op = b_new_var(optype);
	op->r.dst = base[head];

	for (uint32_t i = head; i < tail; i++) {
		if (renamed[i]) {
			/* already in place, VM skips src==dst */
			b_op_pushreg(op, base[i]);
		} else {
			b_op_pushreg(op, b_reg_reg(cb, regs[i]));
		}
	}

	b_emit_var(cb, op);
}

static void b_emit_moveto(b_codeblock *cb, b_reg* base, b_reg *regs, uint32_t cnt) {
	for (uint32_t i = 0; i < cnt; i++)
		b_reg_reg(cb, regs[i]);
	
	b_emit_regmove(cb, B_MOVETO, base, regs, cnt);
}

static void b_emit_movefrom(b_codeblock *cb, b_reg* base, b_reg *regs, uint32_t cnt) {
	b_emit_regmove(cb, B_MOVEFROM, base, regs, cnt);
}

static b_reg b_emit_call(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;

	/* evaluate fn and self before allocating the window */
	b_reg fn;
	b_reg self_reg;

	/* evaluate args into tmps before window allocation */
	b_reg arg_regs[256];
	uint32_t nargs = 0;
	ast_node *args = n->args[1];
	if (args) {
		nargs = b_expr_cnt(args);
	}

	uint32_t nrets = 1;
	uint32_t window_size = 2 + nargs + nrets; /* fn, self, args..., rets... */

	/* release fn + self + args*/
	uint32_t args_end = 2 + nargs;
	
	/* allocate contiguous window */
	b_reg* window = malloc(sizeof(b_reg)*256);
	b_reg_tmp_continuous(cb, window, window_size);

	if (A_TYPE(n->args[0]->type) == A_HASHACCESS) {
		fn = b_emit_method_hashaccess(cb, n->args[0], &self_reg);
	} else {
		fn = b_consume_ast(cb, n->args[0]);
		self_reg = fn;
	}
	
	if (args) {
		nargs = b_expr_cnt(args);
		
		for (uint32_t i = 0; i < nargs; i++)
			arg_regs[i] = b_consume_ast(cb, b_expr_idx(args, i));
	}
	
	/* gather [fn, self, arg0..argN-1] into window via MOVETO */
	b_reg gather[256];
	gather[0] = fn;
	gather[1] = self_reg;
	for (uint32_t i = 0; i < nargs; i++)
		gather[2 + i] = arg_regs[i];

	b_emit_moveto(cb, window, gather, args_end);
	
	b_emit_call_op(cb, window, nargs, nrets);
	
	for (uint32_t i = 0; i < args_end; i++)
		b_reg_release(cb, window[i]);

	// returns cant be renamed as they need to be continuous block
	for (uint32_t i = args_end; i < args_end + nrets; i++)
		b_reg_no_rename(cb, window[i]);
	
	/* ret is at window[2 + nargs] */
	return window[args_end];
}

static b_reg b_declare_one(b_codeblock *cb, ast_node *id) {
	if (A_TYPE(id->type) != A_IDENTIFIER)
		b_error("var expects identifier");
	return b_codeblock_declare(cb, id->token.data, id->token.len);
}

static b_reg b_emit_declare(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;
	ast_node *id = n->args[0];
	uint32_t cnt = b_expr_cnt(id);
	b_reg last = NULL;

	for (uint32_t i = 0; i < cnt; i++) {
		last = b_declare_one(cb, b_expr_idx(id, i));
	}

	return last;
}

static b_reg b_lookup_ident(b_codeblock *cb, ast_node *id, ast_node **ast_glob) {
	b_reg r = b_codeblock_get_ident(cb, id->token.data, id->token.len);
	if (!r) {
		//printf("undefined variable '%s' \n", buf);

		/* create synthetic HASHACCESS AST node: _global.varname
		 * element 0: _global identifier
		 * element 1: varname as STRING (not identifier, to avoid recursion) */
		ast_node *ha = ast_newnode(id->parent, A_HASHACCESS | A_LIST, id->token);
		ha->type = A_HASHACCESS | A_LIST;

		/* element 0: _global identifier */
		ast_node *global_id = b_malloc(sizeof(ast_node));
		memset(global_id, 0, sizeof(ast_node));
		global_id->type = A_IDENTIFIER;
		global_id->token.data = "_global";
		global_id->token.len = 7;
		
		ast_node_push(ha, global_id);

		/* element 1: variable name as STRING node (not identifier)
		 * This avoids infinite recursion when HASHACCESS is consumed */
		ast_node *key_str = b_malloc(sizeof(ast_node));
		memset(key_str, 0, sizeof(ast_node));
		key_str->type = A_STRING;
		key_str->token = id->token;
		ast_node_push(ha, key_str);
		
		*ast_glob = ha;
		return NULL;
	}

	*ast_glob = NULL;
	return r;
}

static b_reg b_emit_identifier(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;
	ast_node *ast_glob = NULL;
	b_reg r = b_lookup_ident(cb, n, &ast_glob);

	if (ast_glob) {
		/* consume the synthetic HASHACCESS AST node */
		return b_consume_ast(cb, ast_glob);
	}

	return r;
}

static void b_assign_one(b_codeblock *cb, b_reg dst, b_reg src) {
	if (b_reg_rename(cb, src, dst))
		return;
	b_emit_r(cb, B_MOVE, dst, src);
	b_reg_release(cb, src);
}

/* emit HASHSTORE for a hash lhs target:
 *   hash.key = val        → HASHSTORE [ R(obj), S("key"), src ]
 *   hash.a.b.key = val    → HASHACCESS to get hash.a.b, then HASHSTORE [ T(tmp), S("key"), src ]
 */
static void b_emit_hashstore(b_codeblock *cb, ast_node *ha, b_reg src) {
	uint32_t cnt = ast_node_list_length(ha);

	b_reg obj;
	b_reg key;

	if (cnt == 2) {
		/* simple: hash.key */
		obj = b_reg_reg(cb, b_consume_ast(cb, ast_node_list(ha, 0)));
		key = b_consume_ast(cb, ast_node_list(ha, 1));
	} else {
		/* chained: hash.a.b.key — read all but last, store into last */
		/* build a HASHACCESS for hash.a.b (all but last key) */

		
		key = b_consume_ast(cb, ci_arr_pop(ha->nodes));
		obj = b_emit_hashaccess(cb, ha, 0);
	}

	/* HASHSTORE is RRR: map_reg, key_reg, val_reg */
	b_emit_rrr(cb, B_HASHSTORE, b_reg_reg(cb, obj), b_reg_reg(cb, key), b_reg_reg(cb, src));
	
	b_reg_release(cb, obj);
	b_reg_release(cb, key);
	b_reg_release(cb, src);
	
}

/* emit ARRAYSTORE for an array lhs target:
 *   arr[idx] = val        → ARRAYSTORE [ R(arr), R(idx), src ]
 *   arr[idx1][idx2] = val → ARRACCESS to get arr[idx1], then ARRAYSTORE [ T(tmp), R(idx2), src ]
 */
static void b_emit_arraystore(b_codeblock *cb, ast_node *aa, b_reg src) {
	/* ARRAYSTORE is RRR: [ array_reg, index_reg, value_reg ] */
	b_reg arr = b_reg_reg(cb, b_consume_ast(cb, aa->args[0]));
	b_reg idx = b_reg_reg(cb, b_consume_ast(cb, aa->args[1]));
	b_reg val = b_reg_reg(cb, src);

	b_emit_rrr(cb, B_ARRAYSTORE, arr, idx, val);
	
	b_reg_release(cb, arr);
	b_reg_release(cb, idx);
	b_reg_release(cb, val);
}

static b_reg b_emit_assign(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;
	ast_node *lhs = n->args[0];
	ast_node *rhs = n->args[1];

	/* if lhs is VAR, declare first, then use its inner expr as lhs */
	if (A_TYPE(lhs->type) == A_VAR) {
		b_emit_declare(cb, lhs, 0);
		lhs = lhs->args[0];
	}

	uint32_t lcnt = b_expr_cnt(lhs);
	uint32_t rcnt = b_expr_cnt(rhs);

	/* assign to identifiers or tmps for hash targets */
	struct { ast_node *ha; b_reg tmp; } deferred[256];
	uint32_t deferred_cnt = 0;
	
	/* special case: rhs is a single CALL — multi-return via MOVEFROM */
	if (rcnt == 1 && A_TYPE(b_expr_idx(rhs, 0)->type) == A_CALL) {
		ast_node *call_node = b_expr_idx(rhs, 0);

		/* emit the call — returns default single ret tmp */
		b_reg default_ret = b_consume_ast(cb, call_node);

		/* find the CALL opcode we just emitted */
		ci_array *bc = cb->func->bytecode;
		b_opcode *call_op = NULL;
		for (uint32_t i = ci_arr_len(bc); i > 0; i--) {
			b_opcode *op = (b_opcode *)ci_arr_index(bc, i - 1);
			if (op->op == B_CALL) { call_op = op; break; }
		}

		/* patch nrets — don't free default_ret, it's inside the window */
		(void)default_ret;
		call_op->call.nrets = lcnt;

		/* ret slots are at base + 2 + nargs, contiguous */

		/* build destination reg list for MOVEFROM */
		b_reg dst_regs[256];
		b_reg last = NULL;
		for (uint32_t i = 0; i < lcnt; i++) {
			ast_node *lid = b_expr_idx(lhs, i);
			ast_node *glob = NULL;

			call_reconsider:;
			uint32_t kind = A_TYPE(lid->type);
			if (kind == A_IDENTIFIER) {
				b_reg dst = b_lookup_ident(cb, lid, &glob);
				if (glob) {
					lid = glob;
					goto call_reconsider;
				}
				dst_regs[i] = dst;
				last = dst;
			} else if (kind == A_HASHACCESS || kind == A_ARRACCESS) {
				b_reg tmp = b_reg_tmp(cb);
				deferred[deferred_cnt].ha = lid;
				deferred[deferred_cnt].tmp = tmp;
				deferred_cnt++;
				dst_regs[i] = tmp;
				last = tmp;
			} else {
				b_error("call ret target must be identifier or map/array access");
			}
		}

		/* scatter rets to destinations */
		uint32_t rets_offset = call_op->call.nargs + 2;
		b_emit_movefrom(cb, call_op->call.all_regs + rets_offset , dst_regs, lcnt);

		/* handle deferred stores */
		for (uint32_t i = 0; i < deferred_cnt; i++)
			b_emit_hashstore(cb, deferred[i].ha, deferred[i].tmp);

		return last;
	}

	if (rcnt > lcnt)
		b_error("too many values on right side of assignment");

	/* phase 1: evaluate rhs*/

	b_reg last = NULL;
	for (uint32_t i = 0; i < rcnt; i++) {
		ast_node *lid = b_expr_idx(lhs, i);
		b_reg src = b_reg_reg(cb, b_consume_ast(cb, b_expr_idx(rhs, i)));
		ast_node *glob = NULL;

		reconsider:;
		uint32_t kind = A_TYPE(lid->type);
		if (kind == A_IDENTIFIER) {
			b_reg dst = b_lookup_ident(cb, lid, &glob);
			if (glob) {
				lid = glob;
				goto reconsider;
			}

			b_assign_one(cb, dst, src);
			last = dst;
		} else if (kind == A_HASHACCESS || kind == A_ARRACCESS) {
			/* assign to tmp, defer hashstore/arraystore */

			deferred[deferred_cnt].ha = lid;
			deferred[deferred_cnt].tmp = src;
			deferred_cnt++;
			last = src;
		} else {
			b_error("assignment target must be identifier or property/array access");
		}
	}

	/* null remaining lhs vars */
	if (lcnt > rcnt) {
		b_opcode *op = b_new_var(B_LOADNULL);
		for (uint32_t i = rcnt; i < lcnt; i++) {
			ast_node *lid = b_expr_idx(lhs, i);
			if (A_TYPE(lid->type) != A_IDENTIFIER)
				b_error("null fill only for identifiers");
			ast_node *glob = NULL;
			b_op_pushreg(op, b_lookup_ident(cb, lid, &glob));
		}
		b_emit_var(cb, op);
	}

	/* phase 2: emit deferred stores */
	for (uint32_t i = 0; i < deferred_cnt; i++) {
		uint32_t kind = A_TYPE(deferred[i].ha->type);
		if (kind == A_HASHACCESS)
			b_emit_hashstore(cb, deferred[i].ha, deferred[i].tmp);
		else if (kind == A_ARRACCESS)
			b_emit_arraystore(cb, deferred[i].ha, deferred[i].tmp);
	}

	return last;
}

static b_reg b_emit_bool_value(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)cb;
	(void)n;
	b_reg dst = b_reg_value();
	dst->type = B_REG_BOOL;
	dst->value.integer = (int64_t)ctx;
	return dst;
}

static b_reg b_emit_return(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;
	b_opcode *op = b_new_var(B_RETURN);

	ast_node *val = n->args[0];
	if (val) {
		uint32_t cnt = b_expr_cnt(val);
		for (uint32_t i = 0; i < cnt; i++) {
			b_op_pushreg(op, b_reg_reg(cb, b_consume_ast(cb, b_expr_idx(val, i))));
		}
	}

	b_emit_var(cb, op);
	return NULL;
}

static b_reg b_emit_if(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;
	b_reg cond = b_reg_reg(cb, b_consume_ast(cb, n->op_if.condition));

	if (n->op_if.else_body) {
		b_reg lbl_else = b_reg_ulabel(cb, "_else");
		b_reg lbl_end  = b_reg_ulabel(cb, "_endif");

		b_emit_jmpf(cb, cond, lbl_else);
		b_reg_release(cb, cond);
		
		b_codeblock *then_cb = b_codeblock_new(cb->func, cb);
		b_consume_codelist(then_cb, n->op_if.body);
		//cb->reg_next = then_cb->reg_next;

		b_emit_jmp(cb, lbl_end);
		b_emit_label(cb, lbl_else);

		/* else_body can be another IF node (else-if chain) or a codeblock */
		if (A_TYPE(n->op_if.else_body->type) == A_IF) {
			b_consume_ast(cb, n->op_if.else_body);
		} else {
			b_codeblock *else_cb = b_codeblock_new(cb->func, cb);
			b_consume_codelist(else_cb, n->op_if.else_body);
			//cb->reg_next = else_cb->reg_next;
		}

		b_emit_label(cb, lbl_end);
	} else {
		b_reg lbl_end = b_reg_ulabel(cb, "_endif");

		b_emit_jmpf(cb, cond, lbl_end);
		b_reg_release(cb, cond);

		b_codeblock *then_cb = b_codeblock_new(cb->func, cb);
		b_consume_codelist(then_cb, n->op_if.body);
		//cb->reg_next = then_cb->reg_next;

		b_emit_label(cb, lbl_end);
	}

	return NULL;
}

static b_reg b_emit_while(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;
	int is_do = !!(n->type & A_DO_LOOP);

	uint32_t loop_id = b_loop_id_next++;

	b_reg lbl_cond = b_ulabel_idx(cb, "wcond", loop_id);
	b_reg lbl_body = b_ulabel_idx(cb, "wbody", loop_id);
	b_reg lbl_end = b_ulabel_idx(cb, "wend", loop_id);

	if (!is_do) {
		b_emit_jmp(cb, lbl_cond);
	}

	b_emit_label(cb, lbl_body);

	b_codeblock *body_cb = b_codeblock_new(cb->func, cb);
	body_cb->current_loop_id = loop_id;
	b_consume_codelist(body_cb, n->op_loop.body);
	cb->reg_next = body_cb->reg_next;

	b_emit_label(cb, lbl_cond);

	b_reg cond = b_reg_reg(cb, b_consume_ast(cb, n->op_loop.condition));
	b_emit_jmpt(cb, cond, lbl_body);

	b_emit_label(cb, lbl_end);

	return NULL;
}

static b_reg b_emit_for(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	uint32_t loop_id = b_loop_id_next++;

	b_reg lbl_cond = b_ulabel_idx(cb, "wcond", loop_id);
	b_reg lbl_body = b_ulabel_idx(cb, "wbody", loop_id);
	b_reg lbl_end = b_ulabel_idx(cb, "wend", loop_id);

	uint32_t var_cnt = b_expr_cnt(n->op_loop.step);
	uint32_t tmp_cnt = var_cnt + 2; // iterator, cursor, ...vars

	b_reg iterable = b_reg_reg(cb, b_consume_ast(cb, n->op_loop.init));

	b_reg tmps[32];
	b_reg_tmp_continuous(cb, tmps, tmp_cnt);

	b_emit_rrr(cb, B_ITERINIT, b_reg_reg(cb, iterable), b_reg_reg(cb, tmps[0]), b_reg_reg(cb, tmps[1]));

	b_emit_label(cb, lbl_cond);
	b_emit_rri32(cb, B_ITERSTEP, tmps[0], lbl_end, tmp_cnt - 2);

	b_emit_label(cb, lbl_body);

	b_codeblock *body_cb = b_codeblock_new(cb->func, cb);
	body_cb->current_loop_id = loop_id;

	/* bind iterator variables (i, v, ...) to tmps[2..] in body scope */
	for (uint32_t i = 0; i < var_cnt; i++) {
		ast_node *id = b_expr_idx(n->op_loop.step, i);
		b_codeblock_declare_reg(body_cb, id->token.data, id->token.len, tmps[2 + i]);
	}

	b_consume_codelist(body_cb, n->op_loop.body);

	b_emit_jmp(cb, lbl_cond);

	b_emit_label(cb, lbl_end);
	b_reg_tmp_release_continuous(cb, tmps, tmp_cnt);

	return NULL;
}

/* walk AST to find leftmost and rightmost token positions,
 * then copy that span from source — produces source-like string */
static void ast_span(ast_node *n, const char **lo, const char **hi) {
	if (!n) return;

	if (n->token.data) {
		const char *end = n->token.data + n->token.len;
		if (!*lo || n->token.data < *lo)
			*lo = n->token.data;
		if (!*hi || end > *hi)
			*hi = end;
	}

	if (n->type & A_LIST) {
		uint32_t cnt = ast_node_list_length(n);
		for (uint32_t i = 0; i < cnt; i++)
			ast_span(ast_node_list(n, i), lo, hi);
	}
}

static char *ast_stringify(ast_node *n) {
	const char *lo = NULL;
	const char *hi = NULL;
	ast_span(n, &lo, &hi);
	if (!lo || !hi || hi <= lo)
		return NULL;

	uint32_t len = (uint32_t)(hi - lo);
	char *s = b_malloc(len + 1);
	memcpy(s, lo, len);
	s[len] = '\0';
	return s;
}

static b_reg b_emit_function(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)ctx;

	/* reentry from b_emit_assign — function already compiled, just load it */
	if (n->op_function.function_id) {
		b_reg dst = b_reg_tmp(cb);
		b_emit_ri32(cb, B_LOADFN, dst, (uint16_t)n->op_function.function_id);
		return dst;
	}

	b_unit *u = cb->func->unit;

	/* generate name */
	char *name;
	if (n->op_function.name) {
		name = ast_stringify(n->op_function.name);
	} else {
		name = b_malloc(32);
		snprintf(name, 32, "anon_%u", u->anon_next++);
	}

	/* create function in unit */
	b_function *f = b_function_new(u, name);
	n->op_function.function_id = f->id;

	/* create root codeblock for function (no parent — no closures yet) */
	b_codeblock *fn_cb = b_codeblock_new(f, NULL);
	f->cb = fn_cb;

	/* declare args (R(0) = self already reserved by b_codeblock_new) */
	ast_node *args = n->op_function.args;
	if (args) {
		uint32_t cnt = b_expr_cnt(args);
		f->arg_count = (uint8_t)cnt;
		for (uint32_t i = 0; i < cnt; i++)
			b_declare_one(fn_cb, b_expr_idx(args, i));
	}

	/* emit body into function's codeblock */
	b_consume_codelist(fn_cb, n->op_function.body);

	/* named function → desugar to: name = function(){...} */
	if (n->op_function.name) {
		ast_node *assign = ast_newnode(n->parent, A_ASSIGN | A_ARG_2, n->token);
		assign->args[0] = n->op_function.name;
		assign->args[1] = n;
		return b_emit_assign(cb, assign, 0);
	}

	/* anonymous — emit LOADFN, reenter hits the guard above */
	return b_emit_function(cb, n, 0);
}

static b_reg b_codegen_emit_label(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	b_reg label = b_reg_label(cb, n->token.data, n->token.len);

	b_emit_label(cb, label);
	
	return label;
}

static b_reg b_emit_goto(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	n = n->args[0];
	if(A_TYPE(n->type) != A_IDENTIFIER){
		b_error("GOTO label must be identifier");
	}

	b_reg label = b_reg_label(cb, n->token.data, n->token.len);

	b_emit_jmp(cb, label);

	return label;
}

static b_reg b_emit_break(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)n;
	(void)ctx;

	uint32_t loop_id = b_get_current_loop_id(cb);
	if (loop_id == 0) {
		b_error("break outside loop");
	}

	b_reg label = b_ulabel_idx(cb, "wend", loop_id);
	b_emit_jmp(cb, label);

	return NULL;
}

static b_reg b_emit_next(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	(void)n;
	(void)ctx;

	uint32_t loop_id = b_get_current_loop_id(cb);
	if (loop_id == 0) {
		b_error("continue outside loop");
	}

	b_reg label = b_ulabel_idx(cb, "wcond", loop_id);
	b_emit_jmp(cb, label);

	return NULL;
}



static b_reg b_emit_assign_infix(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	ast_node *lhs = n->args[0];

	/* fast path: local identifier — operate directly on its register */
	if (A_TYPE(lhs->type) == A_IDENTIFIER) {
		b_reg local = b_codeblock_get_ident(cb, lhs->token.data, lhs->token.len);
		if (local) {
			b_reg src2 = b_consume_ast(cb, n->args[1]);
			b_emit_any(cb, (uint8_t)ctx, local, local, src2);
			b_reg_release(cb, src2);
			return local;
		}
	}

	/* slow path: mutate AST to ASSIGN(lhs, OP(lhs, rhs)) */
	static const struct { uint8_t bop; uint32_t akind; } op_map[] = {
		{ B_ADD, A_ADD }, { B_SUB, A_SUB }, { B_MUL, A_MUL },
		{ B_DIV, A_DIV }, { B_MOD, A_MOD }, { B_POW, A_POW },
		{ B_BIN_OR, A_BIN_OR }, { B_BIN_AND, A_BIN_AND },
		{ B_BIN_XOR, A_BIN_XOR }, { B_BIN_LSHIFT, A_BIN_LSHIFT },
		{ B_BIN_RSHIFT, A_BIN_RSHIFT },
	};
	uint32_t akind = 0;
	for (uint32_t i = 0; i < sizeof(op_map)/sizeof(op_map[0]); i++) {
		if (op_map[i].bop == (uint8_t)ctx) { akind = op_map[i].akind; break; }
	}

	/* create infix node reusing n's args */
	ast_node *op_node = b_malloc(sizeof(ast_node));
	memset(op_node, 0, sizeof(ast_node));
	op_node->type = akind;
	op_node->args[0] = lhs;
	op_node->args[1] = n->args[1];

	n->type = A_ASSIGN;
	n->args[1] = op_node;

	return b_consume_ast(cb, n);
}

static b_reg b_emit_inc_dec(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	b_reg src_dst = b_consume_ast(cb, n->args[0]);

	b_emit_any(cb, (uint8_t)ctx, src_dst, src_dst, b_reg_imm(1));

	return src_dst;
}

static b_reg b_emit_post_inc_dec(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	b_reg src_dst = b_consume_ast(cb, n->args[0]);

	b_reg old = b_reg_tmp(cb);
	b_emit_r(cb, B_MOVE, old, src_dst);
	b_emit_any(cb, (uint8_t)ctx, src_dst, src_dst, b_reg_imm(1));

	return old;
}

/* short-circuit &&/|| — ctx: B_JMPF=&&, B_JMPT=|| */
static b_reg b_emit_shortcircuit(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	b_reg lbl_end = b_reg_ulabel(cb, "_sc");

	b_reg lhs = b_reg_reg(cb, b_consume_ast(cb, n->args[0]));

	/* reuse lhs tmp as result; if named reg, copy to fresh tmp first */
	b_reg result;
	if (b_reg_is_tmp(lhs)) {
		result = lhs;
	} else {
		result = b_reg_tmp(cb);
		b_emit_r(cb, B_MOVE, result, lhs);
	}

	if (ctx == A_NOTNULL) {
		b_reg chk = b_reg_tmp(cb);
		b_emit_r(cb, B_NOTNULL, chk, result);
		b_emit_jmpt_nr(cb, chk, lbl_end);
		b_reg_release(cb, chk);
	} else if (ctx == B_JMPF)
		b_emit_jmpf_nr(cb, result, lbl_end);
	else
		b_emit_jmpt_nr(cb, result, lbl_end);

	b_reg rhs = b_reg_reg(cb, b_consume_ast(cb, n->args[1]));
	b_emit_r(cb, B_MOVE, result, rhs);

	b_emit_label(cb, lbl_end);
	
	b_reg_release(cb, rhs);
	
	return result;
}

/* short-circuit &&= / ||= — mutate to A_ASSIGN inside conditional */
static b_reg b_emit_assign_shortcircuit(b_codeblock *cb, ast_node *n, uint32_t ctx) {
	b_reg lbl_end = b_reg_ulabel(cb, "_sc");

	b_reg val = b_reg_reg(cb, b_consume_ast(cb, n->args[0]));

	if (ctx == A_NOTNULL) {
		b_reg chk = b_reg_tmp(cb);
		b_emit_r(cb, B_NOTNULL, chk, val);
		b_emit_jmpt_nr(cb, chk, lbl_end);
		b_reg_release(cb, chk);
	} else if (ctx == B_JMPF)
		b_emit_jmpf_nr(cb, val, lbl_end);
	else
		b_emit_jmpt_nr(cb, val, lbl_end);

	b_reg_release(cb, val);

	n->type = A_ASSIGN;
	b_consume_ast(cb, n);

	b_emit_label(cb, lbl_end);
	return val;
}

/* ---- dispatch table ---- */

static const b_dispatch_entry b_dispatch[] = {
	{ A_NUMBER,     b_emit_number_value,  0     },
	{ A_STRING,     b_emit_string_value,  0     },
	{ A_IDENTIFIER, b_emit_identifier,    0     },
	{ A_TRUE,       b_emit_bool_value,    1     },
	{ A_FALSE,      b_emit_bool_value,    2     },
	{ A_NULL_LIT,   b_emit_bool_value,    0     },
	{ A_ADD,        b_emit_simple_infix,  B_ADD },
	{ A_SUB,        b_emit_simple_infix,  B_SUB },
	{ A_MUL,        b_emit_simple_infix,  B_MUL },
	{ A_DIV,        b_emit_simple_infix,  B_DIV },
	{ A_MOD,        b_emit_simple_infix,  B_MOD },
	{ A_POW,        b_emit_simple_infix,  B_POW },
	{ A_MINUS,      b_emit_prefix,        B_NEG },
	{ A_NOT,        b_emit_prefix,        B_NOT },
	{ A_BIN_INV,    b_emit_prefix,        B_BIN_INV },
	{ A_BIN_OR,     b_emit_simple_infix,  B_BIN_OR },
	{ A_BIN_AND,    b_emit_simple_infix,  B_BIN_AND },
	{ A_BIN_XOR,    b_emit_simple_infix,  B_BIN_XOR },
	{ A_BIN_LSHIFT, b_emit_simple_infix,  B_BIN_LSHIFT },
	{ A_BIN_RSHIFT, b_emit_simple_infix,  B_BIN_RSHIFT },
	{ A_EQ,         b_emit_simple_infix,  B_EQ  },
	{ A_NEQ,        b_emit_simple_infix,  B_NEQ },
	{ A_GT,         b_emit_simple_infix,  B_GT  },
	{ A_LT,         b_emit_simple_infix,  B_LT  },
	{ A_GT_EQ,      b_emit_simple_infix,  B_GT_EQ },
	{ A_LT_EQ,      b_emit_simple_infix,  B_LT_EQ },
	{ A_METHOD_REF, b_emit_simple_infix,  B_METHODBIND },
	{ A_AND,        b_emit_shortcircuit,       B_JMPF },
	{ A_OR,         b_emit_shortcircuit,       B_JMPT },
	{ A_NOTNULL,    b_emit_shortcircuit,  A_NOTNULL },
	{ A_INC,        b_emit_inc_dec,       B_ADD },
	{ A_DEC,        b_emit_inc_dec,       B_SUB },
	{ A_POST_INC,   b_emit_post_inc_dec,  B_ADD },
	{ A_POST_DEC,   b_emit_post_inc_dec,  B_SUB },
	{ A_ASSIGN_ADD, b_emit_assign_infix,  B_ADD },
	{ A_ASSIGN_SUB, b_emit_assign_infix,  B_SUB },
	{ A_ASSIGN_MUL, b_emit_assign_infix,  B_MUL },
	{ A_ASSIGN_DIV, b_emit_assign_infix,  B_DIV },
	{ A_ASSIGN_MOD, b_emit_assign_infix,  B_MOD },
	{ A_ASSIGN_POW, b_emit_assign_infix,  B_POW },
	{ A_ASSIGN_OR,  b_emit_assign_shortcircuit, B_JMPT },
	{ A_ASSIGN_AND, b_emit_assign_shortcircuit, B_JMPF },
	{ A_ASSIGN_NOTNULL, b_emit_assign_shortcircuit, A_NOTNULL },
	{ A_ASSIGN_BIN_OR,  b_emit_assign_infix, B_BIN_OR },
	{ A_ASSIGN_BIN_AND, b_emit_assign_infix, B_BIN_AND },
	{ A_ASSIGN_BIN_XOR, b_emit_assign_infix, B_BIN_XOR },
	{ A_ASSIGN_LSHIFT,  b_emit_assign_infix, B_BIN_LSHIFT },
	{ A_ASSIGN_RSHIFT,  b_emit_assign_infix, B_BIN_RSHIFT },
	{ A_RETURN,     b_emit_return,        0     },
	{ A_IF,         b_emit_if,            0     },
	{ A_LOOP,       b_emit_while,         0     },
	{ A_FOR_LOOP,     b_emit_for,         0     },
	{ A_VAR,        b_emit_declare,       0     },
	{ A_ASSIGN,     b_emit_assign,        0     },
	{ A_HASHACCESS, b_emit_hashaccess,    0     },
	{ A_MAPACCESS,  b_emit_simple_infix,  B_MAPACCESS },
	{ A_MAP_INIT,   b_emit_map_init,      0     },
	{ A_ARRAY_INIT, b_emit_array_init,    0     },
	{ A_ARRACCESS,  b_emit_arraccess,     0     },
	{ A_CALL,       b_emit_call,          0     },
	{ A_FUNCTION,   b_emit_function,      0     },
	{ A_LABEL,      b_codegen_emit_label, 0     },
	{ A_GOTO,       b_emit_goto,          0     },
	{ A_BREAK,      b_emit_break,         0     },
	{ A_NEXT,       b_emit_next,          0     },
};

#define B_DISPATCH_COUNT (sizeof(b_dispatch) / sizeof(b_dispatch[0]))

/* ---- main walker ---- */

static b_reg b_consume_ast(b_codeblock *cb, ast_node *n) {
	if (!n) {
		fprintf(stderr, "error: null AST node in codegen\n");
		exit(1);
	}

	uint32_t kind = A_TYPE(n->type);

	for (uint32_t i = 0; i < B_DISPATCH_COUNT; i++) {
		if (b_dispatch[i].ast_type == kind) {
			return b_dispatch[i].emit(cb, n, b_dispatch[i].ctx);
		}
	}

	const char *name = (kind < A_COUNT) ? ast_kind_names[kind] : "???";
	fprintf(stderr, "error: unhandled AST node type: %s\n", name);
	exit(1);
}

static void b_consume_codelist(b_codeblock *cb, ast_node *block) {
	if (!block || !(block->type & A_LIST)) {
		fprintf(stderr, "error: expected code block\n");
		exit(1);
	}

	b_reg prev = NULL;
	for (uint32_t i = 0; i < ast_node_list_length(block); i++) {
		if (prev)
			b_reg_release(cb, prev);
		prev = b_consume_ast(cb, ast_node_list(block, i));
	}
	
	if (prev){
		b_reg_release(cb, prev);
	}
}

/* ================================================================
 *  Dumper
 * ================================================================ */

static void b_dump_reg(b_reg r) {
	switch (r->type) {
	case B_REG_TMP:    printf("T(%u)", r->number); break;
	case B_REG_REG:    printf("R(%u)", r->number); break;
	case B_REG_INT:    printf("I(%lld)", (long long)r->value.integer); break;
	case B_REG_DOUBLE: printf("D(%g)", r->value.dbl); break;
	case B_REG_STRING: printf("S(\"%.*s\")", r->strlen, r->value.string); break;
	case B_REG_BOOL: {
		const char *names[] = {"NULL", "TRUE", "FALSE"};
		printf("B(%s)", names[r->value.integer]);
		break;
	}
	case B_REG_LABEL:  printf("L(\"%s\")", r->value.label); break;
	case B_REG_GLOBAL: printf("G(%s)", r->value.global); break;
	default:           printf("?(%u)", r->type); break;
	}
}

static void b_dump_bytecode(b_function *f) {
	uint32_t count = ci_arr_len(f->bytecode);

	for (uint32_t i = 0; i < count; i++) {
		b_opcode *op = (b_opcode *)ci_arr_index(f->bytecode, i);
		const char *name = (op->op < B_OP_COUNT) ? b_op_names[op->op] : "???";

		printf("  [%3u] %-10s ", i, name);

		switch (op->enc) {
		case B_ENC_RRR:
			b_dump_reg(op->rrr.dst);
			printf(", ");
			b_dump_reg(op->rrr.src1);
			printf(", ");
			b_dump_reg(op->rrr.src2);
			break;
		case B_ENC_RRU8:
			b_dump_reg(op->rru8.dst);
			printf(", ");
			b_dump_reg(op->rru8.src1);
			printf(", %u", op->rru8.imm);
			break;
		case B_ENC_RU16:
			b_dump_reg(op->ru16.dst);
			printf(", %u", op->ru16.imm);
			break;
		case B_ENC_RRI:
			b_dump_reg(op->rri32.dst);
			printf(", ");
			b_dump_reg(op->rri32.src1);
			printf(", %u", op->rri32.imm);
			break;
		case B_ENC_RI:
			b_dump_reg(op->rri32.dst);
			printf(", %u", op->rri32.imm);
			break;
			
		case B_ENC_R:
			b_dump_reg(op->r.dst);
			printf(", ");
			b_dump_reg(op->r.src);
			break;
		case B_ENC_R0:
			b_dump_reg(op->r.dst);
			break;
		case B_ENC_VAR_STRID:
		case B_ENC_VAR: {
			uint32_t vcnt = ci_arr_len(op->var.regs);

			if (op->op == B_MOVETO || op->op == B_MOVEFROM) {
				uint8_t base = op->r.dst->number;
				uint8_t end  = base + vcnt - 1;
				if (op->op == B_MOVETO) {
					printf("[");
					for (uint32_t v = 0; v < vcnt; v++) {
						printf("%s", v ? ", " : " ");
						b_dump_reg((b_reg)ci_arr_index(op->var.regs, v));
					}
					printf(" ] -> R(%u)-R(%u)", base, end);
				} else {
					printf("R(%u)-R(%u) -> [", base, end);
					for (uint32_t v = 0; v < vcnt; v++) {
						printf("%s", v ? ", " : " ");
						b_dump_reg((b_reg)ci_arr_index(op->var.regs, v));
					}
					printf(" ]");
				}
				break;
			}

			printf("[");
			for (uint32_t v = 0; v < vcnt; v++) {
				printf("%s", v ? ", " : " ");
				b_dump_reg((b_reg)ci_arr_index(op->var.regs, v));
			}
			printf(" ]");
			if (op->var.rets) {
				uint32_t rcnt = ci_arr_len(op->var.rets);
				printf(" -> [");
				for (uint32_t v = 0; v < rcnt; v++) {
					printf("%s", v ? ", " : " ");
					b_dump_reg((b_reg)ci_arr_index(op->var.rets, v));
				}
				printf(" ]");
			}
			break;
		}
		case B_ENC_CALL: {
			uint8_t base = op->call.base->number;
			uint16_t nargs = op->call.nargs;
			uint16_t nrets = op->call.nrets;
			printf("fn=R(%u) self=R(%u)", base, base + 1);
			if (nargs) {
				printf(" [");
				for (uint16_t a = 0; a < nargs; a++)
					printf("%sR(%u)", a ? ", " : " ", base + 2 + a);
				printf(" ]");
			}
			printf(" -> [");
			for (uint16_t r = 0; r < nrets; r++)
				printf("%sR(%u)", r ? ", " : " ", base + 2 + nargs + r);
			printf(" ]");
			break;
		}
		case B_ENC_DECIDE:
			b_dump_reg(op->decide.dst);
			printf(", ");
			b_dump_reg(op->decide.src1);
			printf(", ");
			b_dump_reg(op->decide.src2);
			break;
		default:
			printf("???");
			break;
		}
		printf("\n");
	}
}

static void b_dump_locals(b_codeblock *cb) {
	if (!cb) return;

	/* collect non-label locals, sort by register number */
	struct { const char *name; uint8_t num; } entries[256];
	uint32_t count = 0;
	uint32_t cursor = 0;
	ci_map_kv *kv;

	while ((kv = ci_map_next(cb->locals, &cursor)) != NULL) {
		b_reg r = (b_reg)kv->val;
		if (r->type == B_REG_LABEL) continue;
		entries[count].name = (const char *)kv->key;
		entries[count].num  = r->number;
		count++;
	}

	for (uint32_t i = 1; i < count; i++) {
		for (uint32_t j = i; j > 0 && entries[j].num < entries[j-1].num; j--) {
			const char *tn = entries[j].name;
			uint8_t tv = entries[j].num;
			entries[j] = entries[j-1];
			entries[j-1].name = tn;
			entries[j-1].num = tv;
		}
	}

	printf("  locals:");
	if (!count) {
		printf(" (none)");
	} else {
		for (uint32_t i = 0; i < count; i++)
			printf("%s %s=R(%u)", i ? "," : "", entries[i].name, entries[i].num);
	}
	printf("\n");
}

static void b_dump_unit(b_unit *u, const char *title) {
	uint32_t fcnt = ci_arr_len(u->functions);
	printf("\n=== %s (%u functions) ===\n", title, fcnt);

	for (uint32_t i = 0; i < fcnt; i++) {
		b_function *f = (b_function *)ci_arr_index(u->functions, i);
		uint32_t opcnt = ci_arr_len(f->bytecode);
		printf("\n--- [%u] %s (%u opcodes) ---\n", f->id, f->name, opcnt);
		b_dump_locals(f->cb);
		b_dump_bytecode(f);
	}
}

/* ================================================================
 *  Main
 * ================================================================ */

#ifndef BYTECODE_NO_MAIN
int main(int argc, char **argv) {
	if (argc < 2) {
		const char msg[] = "usage: bytecode [-d] <source-file>...\n";
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
	ci_map_register();

	for (int i = argi; i < argc; i++) {
		b_parser *p = b_parser_new();
		if (!b_parser_load_file(p, argv[i])) {
			fprintf(stderr, "error: cannot read '%s'\n", argv[i]);
			free(p);
			continue;
		}

		size_t len = ci_str_len(p->buf);
		printf("=== %s (%zu bytes) ===\n", argv[i], len);

		/* parse */
		ast *a = ast_new(p);
		ast_node *block = ast_codelist(a);
		if (!block) {
			fprintf(stderr, "error: parse failed\n");
			ast_free(a);
			b_parser_free(p);
			continue;
		}

		printf("\n=== AST ===\n");
		ast_dump(block, 0);

		/* codegen */
		b_unit *unit = b_unit_new();

		/* top-level code is function 0 ("main") */
		char *main_name = b_malloc(5);
		memcpy(main_name, "main", 5);
		b_function *main_fn = b_function_new(unit, main_name);
		b_codeblock *cb = b_codeblock_new(main_fn, NULL);
		main_fn->cb = cb;
		b_consume_codelist(cb, block);

		/* dump pre-encode */
		b_dump_unit(unit, "Pre-encode");

		/* encoder pass — resolve deferred encodings for all functions */
		uint32_t fcnt = ci_arr_len(unit->functions);
		for (uint32_t fi = 0; fi < fcnt; fi++) {
			b_function *f = (b_function *)ci_arr_index(unit->functions, fi);
			b_encode(f->cb);
		}

		/* dump post-encode */
		b_dump_unit(unit, "Bytecode");

		/* cleanup (not exhaustive — will fix later) */
		ast_node_free(block);
		ast_free(a);
		b_parser_free(p);
	}

	ci_shutdown();
	return 0;
}
#endif /* BYTECODE_NO_MAIN */
