/*
* encoder.c — Citrin binary bytecode encoder + decoder
*
* Build:
*   make encoder
*
* Usage:
*   ./encoder [-d] <source-file>...
*
* Writes <source-file>.cbc and dumps a decoded listing for verification.
*
* File format:
*   "CIBC"              4 B  magic
*   version             u16  = 1
*   string_count        u16
*   function_count      u16
*   --- string table ---
*   [len u16][bytes]  × string_count          (no null terminator)
*   --- functions ---
*   [name_idx u16][arg_count u8][reg_count u8]
*   [local_count u16]                          (0 for now — placeholder)
*   [op_count u16]
*   [...binary opcodes...]
*
* Opcode wire format — first byte: [subtype:2][opnum:6]
*
*   subtype 00  RRR / R / R0   [op][dst][src1][src2]   4 B, unused bytes = 0
*   subtype 01  RRU8           [op][dst][src1][imm8]   4 B
*   subtype 10  RU16           [op][dst][imm_lo][imm_hi] 4 B (LE)
*   subtype 11  VAR            [op][b1][extra_words][0]
*                              + extra_words × 4 B payload
*
* JMP  → subtype 10: dst=0,       imm=target opcode index
* JMPF → subtype 10: dst=cond_reg, imm=target opcode index
* JMPT → subtype 10: dst=cond_reg, imm=target opcode index
*
* RETURN / LOADNULL use the reglist encoding (be_emit_reglist):
*   count  subtype  bytes
*     0      00     [00|op][0][0][0]
*     1      10     [10|op][r0][0][0]         imm=0 ignored
*     2      01     [01|op][r0][r1][0]        imm8=0 ignored
*     3      00     [00|op][r0][r1][r2]
*    >3      11     [11|op][count][extra][0] + regs padded to words
*
*   R(0) is the "no value" sentinel for RETURN slots.
*   self will be made read-only in codegen so it never appears as a return.
*
* CALL → subtype 11:
*   word 0:   [11|CALL][fn_reg][extra_words][self]
*   payload:  [nargs][nrets][arg0][arg1]...[ret0][ret1]... (padded)
*
* LOADINT → subtype 10 for i16 (current codegen limit).
*   TODO: subtype 11 with extra_words=1 for i32, extra_words=2 for i64.
*/

#define BYTECODE_NO_MAIN
#include "bytecode.c"

/* ================================================================
*  Output buffer
* ================================================================ */

typedef struct {
	uint8_t  *data;
	uint32_t  len;
	uint32_t  cap;
} bc_buf;

static bc_buf *bc_buf_new(void) {
	bc_buf *b = b_malloc(sizeof(bc_buf));
	b->data = b_malloc(64);
	b->len  = 0;
	b->cap  = 64;
	return b;
}

static void bc_buf_grow(bc_buf *b, uint32_t need) {
	if (b->len + need <= b->cap) return;
	while (b->cap < b->len + need) b->cap *= 2;
	b->data = realloc(b->data, b->cap);
	if (!b->data) { fprintf(stderr, "out of memory\n"); exit(1); }
}

static void bc_buf_u8(bc_buf *b, uint8_t v) {
	bc_buf_grow(b, 1);
	b->data[b->len++] = v;
}

static void bc_buf_u16(bc_buf *b, uint16_t v) {
	bc_buf_grow(b, 2);
	b->data[b->len++] = (uint8_t)v;
	b->data[b->len++] = (uint8_t)(v >> 8);
}

/* --- Wire subtype prefixes (upper 2 bits of first byte) --- */
#define BC_SUB_RRR   (0u << 6)   /* 00: dst, src1, src2 */
#define BC_SUB_RRU8  (1u << 6)   /* 01: dst, src1, imm8 */
#define BC_SUB_RU16  (2u << 6)   /* 10: dst, imm16      */
#define BC_SUB_VAR   (3u << 6)   /* 11: variable-length  */

/* --- VAR encoding helpers ---
 * VAR opcodes: w0 = [BC_SUB_VAR | opnum][b1][extra_words][b3]
 * followed by extra_words × 4 bytes of payload, padded to 4-byte boundary.
 */

/* ceil(n / 4) — payload bytes → extra 32-bit words needed */
#define BC_CEIL4(n) (((n) + 3u) / 4u)

/* wire size of an interned string ID (u16) */
#define BC_STRID_SIZE 2

/* emit the 4-byte VAR header: [0xC0|op][b1][extra][b3] */
static void bc_buf_var_header(bc_buf *out, uint8_t op, uint8_t b1, uint8_t extra, uint8_t b3) {
	bc_buf_u8(out, BC_SUB_VAR | op);
	bc_buf_u8(out, b1);
	bc_buf_u8(out, extra);
	bc_buf_u8(out, b3);
}

/* pad output to next 4-byte boundary relative to pbytes actually written */
static void bc_buf_pad4(bc_buf *out, uint32_t extra_words, uint32_t pbytes) {
	uint32_t total = extra_words * 4;
	for (uint32_t i = pbytes; i < total; i++)
		bc_buf_u8(out, 0);
}

static void bc_buf_bytes(bc_buf *b, const void *src, uint32_t n) {
	bc_buf_grow(b, n);
	memcpy(b->data + b->len, src, n);
	b->len += n;
}

/* ================================================================
*  String table
* ================================================================ */

typedef struct {
	char     **strs;
	uint32_t  *lens;
	uint32_t   count;
	uint32_t   cap;
} bc_strtab;

static bc_strtab *bc_strtab_new(void) {
	bc_strtab *t = b_malloc(sizeof(bc_strtab));
	t->cap   = 16;
	t->count = 0;
	t->strs  = b_malloc(t->cap * sizeof(char *));
	t->lens  = b_malloc(t->cap * sizeof(uint32_t));
	return t;
}

static uint16_t bc_strtab_intern(bc_strtab *t, const char *s, uint32_t len) {
	for (uint32_t i = 0; i < t->count; i++) {
		if (t->lens[i] == len && memcmp(t->strs[i], s, len) == 0)
			return (uint16_t)i;
	}
	if (t->count == t->cap) {
		t->cap *= 2;
		t->strs = realloc(t->strs, t->cap * sizeof(char *));
		t->lens = realloc(t->lens, t->cap * sizeof(uint32_t));
		if (!t->strs || !t->lens) { fprintf(stderr, "out of memory\n"); exit(1); }
	}
	char *copy = b_malloc(len + 1);
	memcpy(copy, s, len);
	copy[len] = '\0';
	t->strs[t->count] = copy;
	t->lens[t->count] = len;
	return (uint16_t)(t->count++);
}

/* label map: ci_map from label name → (opcode_index + 1) as ci_ptr.
* +1 so idx=0 is distinct from "not found" (ci_map returns 0 for missing). */

static void bc_label_set(ci_map *m, char *name, uint16_t idx) {
	ci_map_set_str(m, name, (ci_ptr)(uintptr_t)(idx + 1));
}

static uint16_t bc_label_get(ci_map *m, const char *name) {
	ci_ptr v = ci_map_get_str(m, name);
	if (!v) b_error("encoder: undefined label '%s'", name);
	return (uint16_t)(uintptr_t)(v - 1);
}

/* ================================================================
*  Reglist encoder — shared by RETURN and LOADNULL
*
*  Always VAR subtype:  [11|op][count][extra_words][first_reg]
*
*  count  extra  layout
*    0      0    [11|op][0][0][0]
*    1      0    [11|op][1][0][r0]          — first reg in header word
*   >1      N    [11|op][N][extra][r0] + [r1..rN-1] zero-padded to words
*                extra = ceil((count-1) / 4)
* ================================================================ */

static void be_emit_reglist(bc_buf *out, uint8_t opnum,
							uint8_t *regs, uint32_t count) {
	if (count <= 1) {
		bc_buf_u8(out, BC_SUB_VAR | opnum);
		bc_buf_u8(out, (uint8_t)count);
		bc_buf_u8(out, 0);
		bc_buf_u8(out, count == 1 ? regs[0] : 0);
		return;
	}
	if (count > 255) b_error("reglist: count > 255");
	uint32_t remaining = count - 1;
	uint32_t extra = BC_CEIL4(remaining);
	if (extra > 255) b_error("reglist: payload overflow");
	bc_buf_u8(out, BC_SUB_VAR | opnum);
	bc_buf_u8(out, (uint8_t)count);
	bc_buf_u8(out, (uint8_t)extra);
	bc_buf_u8(out, regs[0]);
	for (uint32_t i = 1; i < count; i++) bc_buf_u8(out, regs[i]);
	bc_buf_pad4(out, extra, remaining);
}

/* ================================================================
*  Function binary encoder
* ================================================================ */

typedef struct {
	bc_buf   *code;
	uint16_t  op_count;
} bc_fn_code;

/* word count an IR op will encode to (1 for all fixed ops, 1+extra for VAR) */
static uint32_t b_op_word_count(b_opcode *op) {
	if (op->enc != B_ENC_VAR && op->enc != B_ENC_VAR_STRID) return 1;

	uint32_t vcnt   = ci_arr_len(op->var.regs);

	if (op->op == B_RETURN || op->op == B_LOADNULL) {
		if (vcnt <= 1) return 1;
		return 1 + BC_CEIL4(vcnt - 1);
	}
	if (op->op == B_CALL) {
		uint32_t nrets = op->var.rets ? ci_arr_len(op->var.rets) : 0;
		uint32_t pbytes = 2 + (vcnt - 2) + nrets;
		return 1 + BC_CEIL4(pbytes);
	}
	if (op->op == B_NEWMAP) {
		if (vcnt < 1) return 1;
		uint32_t pair_count = (vcnt - 1) / 2;
		uint32_t pbytes = 0;

		if (op->enc == B_ENC_VAR_STRID) {
			/* VAR_STRID: [u8 val][u16 strid] per pair */
			pbytes = pair_count * (1 + BC_STRID_SIZE);
		} else {
			/* VAR: [u8 type][u8 val][key] per pair */
			for (uint32_t i = 0; i < pair_count; i++) {
				b_reg key = (b_reg)ci_arr_index(op->var.regs, 1 + (i * 2) + 1);
				pbytes += 2;  /* type (u8) + value (u8) */
				pbytes += (key->type == B_REG_STRING) ? BC_STRID_SIZE : 1;  /* key: strid or reg (u8) */
			}
		}
		return 1 + BC_CEIL4(pbytes);  /* +3 aligns to 4-byte words */
	}
	if (op->op == B_NEWARRAY) {
		if (vcnt < 1) return 1;
		uint32_t elem_count = vcnt - 1;
		uint32_t pbytes = elem_count;  /* [u8 reg] per element */
		return 1 + BC_CEIL4(pbytes);  /* +3 aligns to 4-byte words */
	}
	if (op->op == B_HASHACCESS) {
		if (vcnt < 2) return 1;
		uint32_t string_count = vcnt - 2;
		uint32_t pbytes = 1 + string_count * BC_STRID_SIZE;  /* 1 byte src_reg + strid per key */
		return 1 + BC_CEIL4(pbytes);  /* +3 aligns to 4-byte words */
	}
	return 1;
}

static bc_fn_code be_encode_function(b_function *f) {

	bc_buf  *out    = bc_buf_new();
	ci_map  *labels = ci_map_new(16);
	uint32_t total  = ci_arr_len(f->bytecode);
	uint16_t word_pos = 0;

	/* pass 1: map label names to their absolute word position */
	for (uint32_t i = 0; i < total; i++) {
		b_opcode *op = (b_opcode *)ci_arr_index(f->bytecode, i);
		if (op->op == B_LABEL)
			bc_label_set(labels, op->r.dst->value.label, word_pos);
		else
			word_pos += (uint16_t)b_op_word_count(op);
	}
	uint16_t op_count = word_pos;

	/* pass 2: emit binary */
	for (uint32_t i = 0; i < total; i++) {
		b_opcode *op = (b_opcode *)ci_arr_index(f->bytecode, i);
		if (op->op == B_LABEL) continue;


		switch (op->enc) {

		case B_ENC_RRR:
			bc_buf_u8(out, BC_SUB_RRR | op->op);
			bc_buf_u8(out, op->rrr.dst->number);
			bc_buf_u8(out, op->rrr.src1->number);
			bc_buf_u8(out, op->rrr.src2->number);
			break;

		case B_ENC_RRU8:
			bc_buf_u8(out, BC_SUB_RRU8 | op->op);
			bc_buf_u8(out, op->rru8.dst->number);
			bc_buf_u8(out, op->rru8.src1->number);
			bc_buf_u8(out, op->rru8.imm);
			break;

		case B_ENC_RU16:
			/* LOADINT, LOADFN, LOADSTR, and DECIDE-resolved ops */
			bc_buf_u8(out, BC_SUB_RU16 | op->op);
			bc_buf_u8(out, op->ru16.dst->number);
			bc_buf_u8(out, (uint8_t)(op->ru16.imm));
			bc_buf_u8(out, (uint8_t)(op->ru16.imm >> 8));
			break;

		case B_ENC_R:
			if (op->op == B_JMPF || op->op == B_JMPT) {
				/* dst = cond reg, src = label */
				uint16_t target = bc_label_get(labels, op->r.src->value.label);
				bc_buf_u8(out, BC_SUB_RU16 | op->op);
				bc_buf_u8(out, op->r.dst->number);
				bc_buf_u8(out, (uint8_t)target);
				bc_buf_u8(out, (uint8_t)(target >> 8));
			} else {
				/* MOVE, NEG, NOT, BIN_INV */
				bc_buf_u8(out, BC_SUB_RRR | op->op);
				bc_buf_u8(out, op->r.dst->number);
				bc_buf_u8(out, op->r.src->number);
				bc_buf_u8(out, 0);
			}
			break;

		case B_ENC_R0:
			if (op->op == B_JMP) {
				/* dst holds label, no cond register */
				uint16_t target = bc_label_get(labels, op->r.dst->value.label);
				bc_buf_u8(out, BC_SUB_RU16 | op->op);
				bc_buf_u8(out, 0);
				bc_buf_u8(out, (uint8_t)target);
				bc_buf_u8(out, (uint8_t)(target >> 8));
			} else if (op->op == B_LOADNULL) {
				/* single-target LOADNULL — always VAR reglist encoding */
				uint8_t reg = op->r.dst->number;
				be_emit_reglist(out, op->op, &reg, 1);
			} else {
				/* LOADTRUE, LOADFALSE */
				bc_buf_u8(out, BC_SUB_RRR | op->op);
				bc_buf_u8(out, op->r.dst->number);
				bc_buf_u8(out, 0);
				bc_buf_u8(out, 0);
			}
			break;

		case B_ENC_VAR: {
			uint32_t vcnt   = ci_arr_len(op->var.regs);
			uint32_t rcount = op->var.rets ? ci_arr_len(op->var.rets) : 0;

			if (op->op == B_NEWMAP) {
				/* NEWMAP dst, pairs of [type, value, key]
				* w0: [11|NEWMAP][dst][extra_words][0]
				* payload: [type1][val1][key1] [type2][val2][key2]...
				* type=1: key is u8 register
				* type=0: key is u16 string ID */
				if (vcnt < 1)
					b_error("NEWMAP: need at least dst register");
				if ((vcnt - 1) % 2 != 0)
					b_error("NEWMAP: need pairs of [value, key]");

				b_reg dst = (b_reg)ci_arr_index(op->var.regs, 0);
				uint32_t pair_count = (vcnt - 1) / 2;

				/* calculate payload size */
				uint32_t pbytes = 0;
				for (uint32_t i = 0; i < pair_count; i++) {
					b_reg key = (b_reg)ci_arr_index(op->var.regs, 1 + (i * 2) + 1);
					pbytes += 2; /* type + value */
					pbytes += (key->type == B_REG_STRING) ? 2 : 1; /* key */
				}

				uint32_t extra = BC_CEIL4(pbytes);
				if (extra > 255)
					b_error("NEWMAP: too many pairs");

				bc_buf_var_header(out, op->op, dst->number, (uint8_t)extra, 0);

				for (uint32_t i = 0; i < pair_count; i++) {
					b_reg val = (b_reg)ci_arr_index(op->var.regs, 1 + (i * 2));
					b_reg key = (b_reg)ci_arr_index(op->var.regs, 1 + (i * 2) + 1);

					if (key->type == B_REG_STRING) {
						bc_buf_u8(out, 0); /* type=0: strid */
						bc_buf_u8(out, val->number);
						uint16_t strid = key->interned_string_id;
						bc_buf_u8(out, (uint8_t)strid);
						bc_buf_u8(out, (uint8_t)(strid >> 8));
					} else {
						bc_buf_u8(out, 1); /* type=1: register */
						bc_buf_u8(out, val->number);
						bc_buf_u8(out, key->number);
					}
				}

				bc_buf_pad4(out, extra, pbytes);

			} else if (op->op == B_NEWARRAY) {
				/* NEWARRAY dst, [r0, r1, ..., rN]
				* w0: [11|NEWARRAY][dst][extra_words][regct]
				* payload: [r0][r1]...[rN] [padding] */
				if (vcnt < 1)
					b_error("NEWARRAY: need at least dst register");

				b_reg dst = (b_reg)ci_arr_index(op->var.regs, 0);
				uint32_t elem_count = vcnt - 1;

				uint32_t pbytes = elem_count;  /* [u8 reg] per element */
				uint32_t extra = BC_CEIL4(pbytes);
				if (extra > 255)
					b_error("NEWARRAY: too many elements");

				bc_buf_var_header(out, op->op, dst->number, (uint8_t)extra, (uint8_t)elem_count);

				for (uint32_t i = 1; i < vcnt; i++) {
					b_reg elem = (b_reg)ci_arr_index(op->var.regs, i);
					bc_buf_u8(out, elem->number);
				}

				bc_buf_pad4(out, extra, pbytes);

			} else if (op->op == B_RETURN) {
				if (vcnt > 3) b_error("RETURN: >3 values not yet supported");
				uint8_t regs[3];
				for (uint32_t v = 0; v < vcnt; v++)
					regs[v] = ((b_reg)ci_arr_index(op->var.regs, v))->number;
				be_emit_reglist(out, op->op, regs, vcnt);

			} else if (op->op == B_LOADNULL) {
				/* multi-target LOADNULL */
				uint8_t regs[256];
				for (uint32_t v = 0; v < vcnt; v++)
					regs[v] = ((b_reg)ci_arr_index(op->var.regs, v))->number;
				be_emit_reglist(out, op->op, regs, vcnt);

			} else if (op->op == B_CALL) {
				if (vcnt < 2) b_error("CALL: malformed var (need fn + self)");
				b_reg fn   = (b_reg)ci_arr_index(op->var.regs, 0);
				b_reg self = (b_reg)ci_arr_index(op->var.regs, 1);
				uint32_t nargs = vcnt - 2;
				uint32_t nrets = op->var.rets ? ci_arr_len(op->var.rets) : 0;

				/* payload: [nargs:u8][nrets:u8][arg_regs...][ret_regs...] */
				uint32_t pbytes = 2 + nargs + nrets;
				uint32_t extra  = BC_CEIL4(pbytes);
				if (extra > 255) b_error("CALL: too many args/rets");

				bc_buf_var_header(out, op->op, fn->number, (uint8_t)extra, self->number);

				bc_buf_u8(out, (uint8_t)nargs);
				bc_buf_u8(out, (uint8_t)nrets);
				for (uint32_t v = 2; v < vcnt; v++)
					bc_buf_u8(out, ((b_reg)ci_arr_index(op->var.regs, v))->number);
				if (op->var.rets) {
					for (uint32_t v = 0; v < nrets; v++)
						bc_buf_u8(out, ((b_reg)ci_arr_index(op->var.rets, v))->number);
				}
				bc_buf_pad4(out, extra, pbytes);

			} else {
				b_error("encoder: unhandled VAR op '%s'", b_op_names[op->op]);
			}
			break;
		}

		case B_ENC_VAR_STRID: {
			/* B_ENC_VAR_STRID for HASHACCESS or NEWMAP with string keys
			* HASHACCESS: w0: [op][dst][extra_words][string_count]
			*             w1-wN: [u16 strid]... (2 IDs per word, padded)
			* NEWMAP: w0: [op][dst][extra_words][pair_count]
			*         w1-wN: [u8 val_reg][u16 strid_key]... pairs, padded */
			uint32_t vcnt = ci_arr_len(op->var.regs);
			if (op->op != B_HASHACCESS && op->op != B_NEWMAP)
				b_error("VAR_STRID: only HASHACCESS and NEWMAP supported");

			if (op->op == B_NEWMAP) {
				/* NEWMAP [ dst, val1, key1, val2, key2, ... ] */
				uint32_t pair_count = (vcnt - 1) / 2;
				uint32_t pbytes = pair_count * (1 + BC_STRID_SIZE);  /* [u8 val][u16 strid] per pair */
				uint32_t extra = BC_CEIL4(pbytes);
				if (extra > 255)
					b_error("NEWMAP VAR_STRID: too many pairs");

				b_reg dst = (b_reg)ci_arr_index(op->var.regs, 0);
				bc_buf_var_header(out, op->op, dst->number, (uint8_t)extra, (uint8_t)pair_count);

				for (uint32_t i = 0; i < pair_count; i++) {
					b_reg val = (b_reg)ci_arr_index(op->var.regs, 1 + (i * 2));
					b_reg key = (b_reg)ci_arr_index(op->var.regs, 1 + (i * 2) + 1);
					if (key->type != B_REG_STRING)
						b_error("NEWMAP VAR_STRID: key must be string, got type %u", key->type);
					bc_buf_u8(out, val->number);
					uint16_t strid = key->interned_string_id;
					bc_buf_u8(out, (uint8_t)strid);
					bc_buf_u8(out, (uint8_t)(strid >> 8));
				}
				bc_buf_pad4(out, extra, pbytes);
				break;
			}

			if (vcnt < 2)
				b_error("HASHACCESS VAR_STRID: need dst + source");

			b_reg dst = (b_reg)ci_arr_index(op->var.regs, 0);
			b_reg src = (b_reg)ci_arr_index(op->var.regs, 1);
			uint32_t string_count = vcnt - 2;

			/* payload: [u8 src_reg][u16 strid per key...] */
			uint32_t pbytes = 1 + string_count * BC_STRID_SIZE;
			uint32_t extra = BC_CEIL4(pbytes);
			if (extra > 255)
				b_error("HASHACCESS: too many string keys");

			bc_buf_var_header(out, op->op, dst->number, (uint8_t)extra, (uint8_t)string_count);

			bc_buf_u8(out, src->number);
			for (uint32_t v = 2; v < vcnt; v++) {
				b_reg r = (b_reg)ci_arr_index(op->var.regs, v);
				if (r->type != B_REG_STRING)
					b_error("HASHACCESS VAR_STRID: key must be string, got type %u", r->type);
				uint16_t strid = r->interned_string_id;
				bc_buf_u8(out, (uint8_t)strid);
				bc_buf_u8(out, (uint8_t)(strid >> 8));
			}
			bc_buf_pad4(out, extra, pbytes);
			break;
		}

		case B_ENC_DECIDE:
			b_error("encoder: unresolved DECIDE for '%s' — run b_encode first",
					b_op_names[op->op]);
			break;

		default:
			b_error("encoder: unknown enc %u for '%s'", op->enc, b_op_names[op->op]);
		}
	}

	bc_fn_code result;
	result.code     = out;
	result.op_count = op_count;
	return result;
}

/* ================================================================
*  Unit binary writer
* ================================================================ */

static bc_buf *be_encode_unit(b_unit *unit) {
	uint32_t fcnt = ci_arr_len(unit->functions);

	/* string table: code strings first (indices must match LOADSTR immediates),
	* then function names */
	bc_strtab *strtab = bc_strtab_new();
	uint32_t str_cnt = ci_arr_len(unit->str_pool);
	for (uint32_t i = 0; i < str_cnt; i++) {
		ci_str *s = (ci_str *)ci_arr_index(unit->str_pool, i);
		uint32_t len = ci_str_len(s);
		uint8_t *data = ci_str_head(s);
		bc_strtab_intern(strtab, (const char *)data, len);
	}
	for (uint32_t i = 0; i < fcnt; i++) {
		b_function *f = (b_function *)ci_arr_index(unit->functions, i);
		bc_strtab_intern(strtab, f->name, (uint32_t)strlen(f->name));
	}

	/* encode all function bodies */
	bc_fn_code *fn_codes = b_malloc(fcnt * sizeof(bc_fn_code));
	for (uint32_t i = 0; i < fcnt; i++) {
		b_function *f = (b_function *)ci_arr_index(unit->functions, i);
		fn_codes[i] = be_encode_function(f);
	}

	bc_buf *out = bc_buf_new();

	/* header */
	bc_buf_bytes(out, "CIBC", 4);
	bc_buf_u16(out, 1);                        /* version */
	bc_buf_u16(out, (uint16_t)strtab->count);
	bc_buf_u16(out, (uint16_t)fcnt);

	/* string table */
	for (uint32_t i = 0; i < strtab->count; i++) {
		bc_buf_u16(out, (uint16_t)strtab->lens[i]);
		bc_buf_bytes(out, strtab->strs[i], strtab->lens[i]);
	}

	/* functions */
	for (uint32_t i = 0; i < fcnt; i++) {
		b_function *f  = (b_function *)ci_arr_index(unit->functions, i);
		bc_fn_code *fc = &fn_codes[i];

		uint16_t name_idx  = bc_strtab_intern(strtab, f->name, (uint32_t)strlen(f->name));
		uint8_t  reg_count = f->cb ? f->cb->reg_next : 0;

		bc_buf_u16(out, name_idx);
		bc_buf_u8 (out, 0);           /* arg_count — TODO: track in b_function */
		bc_buf_u8 (out, reg_count);
		bc_buf_u16(out, 0);           /* local_count = 0 (placeholder for debug info) */
		/* no locals written — local_count is 0 */
		bc_buf_u16(out, fc->op_count);
		bc_buf_bytes(out, fc->code->data, fc->code->len);
	}

	free(fn_codes);
	return out;
}

/* ================================================================
*  Decoder / dump
* ================================================================ */

typedef struct {
	const uint8_t *data;
	uint32_t       pos;
	uint32_t       len;
} bc_reader;

static uint8_t  bcr_u8 (bc_reader *r) {
	if (r->pos + 1 > r->len) b_error("decode: unexpected end of file");
	return r->data[r->pos++];
}
static uint16_t bcr_u16(bc_reader *r) {
	uint16_t v = bcr_u8(r);
	return v | ((uint16_t)bcr_u8(r) << 8);
}

static void bc_dump(const uint8_t *data, uint32_t len) {
	bc_reader rr = { data, 0, len };
	bc_reader *r = &rr;

	/* header */
	char magic[5] = {0};
	for (int i = 0; i < 4; i++) magic[i] = (char)bcr_u8(r);
	uint16_t version = bcr_u16(r);
	uint16_t str_cnt = bcr_u16(r);
	uint16_t fn_cnt  = bcr_u16(r);

	printf("magic=%.4s version=%u strings=%u functions=%u\n",
		magic, version, str_cnt, fn_cnt);

	/* string table */
	char **strs = b_malloc(str_cnt * sizeof(char *));
	printf("\n--- strings (%u) ---\n", str_cnt);
	for (uint16_t i = 0; i < str_cnt; i++) {
		uint16_t slen = bcr_u16(r);
		strs[i] = b_malloc((uint32_t)slen + 1);
		for (uint16_t c = 0; c < slen; c++) strs[i][c] = (char)bcr_u8(r);
		strs[i][slen] = '\0';
		printf("  [%u] \"%s\"\n", i, strs[i]);
	}

	/* functions */
	for (uint16_t fi = 0; fi < fn_cnt; fi++) {
		uint16_t name_idx  = bcr_u16(r);
		uint8_t  arg_count = bcr_u8(r);
		uint8_t  reg_count = bcr_u8(r);
		uint16_t loc_count = bcr_u16(r);
		for (uint16_t li = 0; li < loc_count; li++) { bcr_u16(r); bcr_u8(r); }
		uint16_t op_count  = bcr_u16(r);

		const char *fname = (name_idx < str_cnt) ? strs[name_idx] : "?";
		printf("\n--- fn[%u] '%s'  args=%u regs=%u ops=%u ---\n",
			fi, fname, arg_count, reg_count, op_count);

		uint32_t word_idx = 0;
		for (uint16_t oi = 0; oi < op_count; oi++) {
			uint8_t     b0      = bcr_u8(r);
			uint8_t     subtype = b0 >> 6;
			uint8_t     opnum   = b0 & 0x3F;
			const char *oname   = (opnum < B_OP_COUNT) ? b_op_names[opnum] : "???";

			printf("  [%3u] %-12s", word_idx++, oname);

			switch (subtype) {
			case 0: {
				uint8_t b1 = bcr_u8(r), b2 = bcr_u8(r), b3 = bcr_u8(r);
				printf("%u, %u, %u", b1, b2, b3);
				break;
			}
			case 1: {
				uint8_t b1 = bcr_u8(r), b2 = bcr_u8(r), imm = bcr_u8(r);
				printf("%u, %u, [%u]", b1, b2, imm);
				break;
			}
			case 2: {
				uint8_t  b1  = bcr_u8(r);
				uint16_t imm = bcr_u8(r);
				imm |= (uint16_t)bcr_u8(r) << 8;
				if (opnum == B_JMP || opnum == B_JMPF || opnum == B_JMPT) {
					int32_t rel = (int32_t)imm - (int32_t)(word_idx - 1);
					printf("%u, %u [%+d]", b1, imm, rel);
				} else {
					printf("%u, [%u]", b1, imm);
				}
				break;
			}
			case 3: {
				uint8_t  b1    = bcr_u8(r);
				uint8_t  extra = bcr_u8(r);
				uint8_t  b3    = bcr_u8(r);
				uint8_t  payload[256];
				uint32_t pbytes = (uint32_t)extra * 4;
				for (uint32_t k = 0; k < pbytes; k++) payload[k] = bcr_u8(r);

				printf("%u, [%u], %u", b1, extra, b3);
				for (uint8_t w = 0; w < extra; w++) {
					uint32_t base = (uint32_t)w * 4;
					if (w == 0)
						printf("\n  [%3u]           ( ", word_idx);
					else
						printf("\n  [%3u]             ", word_idx);
					word_idx++;
					for (int k = 0; k < 4; k++) {
						int last = (w == extra - 1) && (k == 3);
						printf("%u%s", payload[base + k], last ? " )" : ", ");
					}
				}
				break;
			}
			}
			printf("\n");
		}

	}

	for (uint16_t i = 0; i < str_cnt; i++) free(strs[i]);
	free(strs);
}

/* ================================================================
*  Main
* ================================================================ */

#ifndef ENCODER_NO_MAIN
int main(int argc, char **argv) {
	if (argc < 2) {
		const char msg[] = "usage: encoder [-d] <source-file>...\n";
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

		printf("=== %s ===\n", argv[i]);

		ast *a = ast_new(p);
		ast_node *block = ast_codelist(a);
		if (!block) {
			fprintf(stderr, "error: parse failed\n");
			ast_free(a);
			b_parser_free(p);
			continue;
		}

		/* codegen */
		b_unit *unit = b_unit_new();
		char *main_name = b_malloc(5);
		memcpy(main_name, "main", 5);
		b_function *main_fn = b_function_new(unit, main_name);
		b_codeblock *cb = b_codeblock_new(main_fn, NULL);
		main_fn->cb = cb;
		b_consume_codelist(cb, block);

		/* IR encode pass */
		uint32_t fcnt = ci_arr_len(unit->functions);
		for (uint32_t fi = 0; fi < fcnt; fi++) {
			b_function *f = (b_function *)ci_arr_index(unit->functions, fi);
			b_encode(f->cb);
		}

		b_dump_unit(unit, "Bytecode IR");

		/* binary encode */
		bc_buf *binary = be_encode_unit(unit);
		printf("\n=== Binary (%u bytes) ===\n", binary->len);

		/* write .cbc */
		char outpath[4096];
		snprintf(outpath, sizeof(outpath), "%s.cbc", argv[i]);
		FILE *fp = fopen(outpath, "wb");
		if (fp) {
			fwrite(binary->data, 1, binary->len, fp);
			fclose(fp);
			printf("written: %s\n", outpath);
		} else {
			fprintf(stderr, "warning: cannot write '%s'\n", outpath);
		}

		/* decode + dump for verification */
		printf("\n=== Decoded ===\n");
		bc_dump(binary->data, binary->len);

		ast_node_free(block);
		ast_free(a);
		b_parser_free(p);
	}

	ci_shutdown();
	return 0;
}
#endif /* ENCODER_NO_MAIN */
