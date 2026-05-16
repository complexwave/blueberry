/*
* encoder.c — Citrin bytecode encoder v2 + decoder
*
* New wire format (version 2):
*
*   Every instruction is 8 bytes:
*     [op:u8] [r1:u8] [r2:u8] [r3:u8] [imm:i32-LE]
*
*   Top 2 bits of op byte = subtype:
*     00  RRR  r1,r2,r3 are register numbers; imm = 0
*     01  RRI  r1,r2,r3 are register numbers; imm = signed integer
*     10  RRS  r1,r2,r3 are register numbers; imm = string table id (u32)
*     11  VAR  r1,r2 as noted per-op; r3 = payload word count;
*              imm = per-op; followed by r3*4 bytes of payload
*
*   Register byte conventions (common):
*     r1 = dst
*     r2 = src1 / obj / fn
*     r3 = src2 / key / nwords (VAR)
*
*   Jump targets are absolute opcode indices stored in imm (i32).
*   r1 = cond reg for JMPF/JMPT (0 = unused for JMP).
*
*   VAR payload layouts:
*
*     CALL    RRR: r1=base r2=nargs r3=nrets  imm=0
*             window: stack[base..base+1+nargs+nrets] = [fn][self][args...][rets...]
*
*     RETURN  r1=nrets r2=0 r3=nwords
*             payload: [r0:u8][r1:u8]...
*
*     LOADNULL r1=nregs r2=0 r3=nwords
*             payload: [d0:u8][d1:u8]...
*
*     NEWARRAY r1=dst r2=nelem r3=nwords
*             payload: [e0:u8][e1:u8]...
*
*     NEWMAP  r1=dst r2=npairs r3=nwords
*             payload: per pair: [type:u8][val:u8][key_lo:u8][key_hi:u8]
*               type 0 = key is strid (u16 in key_lo/key_hi)
*               type 1 = key is register (key_lo = reg, key_hi = 0)
*
*     HASHACCESS r1=dst r2=src r3=nwords
*             payload: [strid_lo:u8][strid_hi:u8] per key, padded to 4-byte words
*             nwords = ceil(nkeys / 2)
*
*   File format:
*     "CIBC"           4 B  magic
*     version          u16  = 2
*     string_count     u16
*     function_count   u16
*     --- string table ---
*     [len:u16][bytes]  x string_count   (no null terminator)
*     --- functions ---
*     [name_idx:u16][arg_count:u8][reg_count:u8]
*     [local_count:u16]
*     [code_bytes:u32]
*     [...binary instructions, each 8 bytes...]
*/

#define BYTECODE_NO_MAIN
#include "bytecode.c"

/* ================================================================
 *  Output buffer  (backed by ci_str internally)
 * ================================================================ */

typedef struct {
	ci_str *s;
} bc_buf;

static bc_buf *bc_buf_new(void) {
	bc_buf *b = b_malloc(sizeof(bc_buf));
	b->s = ci_str_new(256);
	return b;
}

static void bc_buf_free(bc_buf *b) {
	ci_free(b->s);
	free(b);
}

static uint8_t *bc_buf_data(bc_buf *b) {
	return ci_str_head(b->s);
}

static uint32_t bc_buf_len(bc_buf *b) {
	return (uint32_t)ci_str_len(b->s);
}

static void bc_buf_u8(bc_buf *b, uint8_t v) {
	ci_str_append(b->s, &v, 1);
}

static void bc_buf_u16(bc_buf *b, uint16_t v) {
	uint8_t tmp[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
	ci_str_append(b->s, tmp, 2);
}

static void bc_buf_u32(bc_buf *b, uint32_t v) {
	uint8_t tmp[4] = {
		(uint8_t)v, (uint8_t)(v >> 8),
		(uint8_t)(v >> 16), (uint8_t)(v >> 24)
	};
	ci_str_append(b->s, tmp, 4);
}

static void bc_buf_bytes(bc_buf *b, const void *src, uint32_t n) {
	ci_str_append(b->s, src, n);
}

/* write LE u32 at a specific byte offset (for backpatching) */
static void bc_buf_patch_u32(bc_buf *b, uint32_t offset, uint32_t v) {
	uint8_t *p = ci_str_head(b->s) + offset;
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* pad to next 4-byte boundary after having written pbytes of payload */
static void bc_buf_pad4(bc_buf *b, uint32_t nwords, uint32_t pbytes) {
	uint32_t total = nwords * 4;
	for (uint32_t i = pbytes; i < total; i++)
		bc_buf_u8(b, 0);
}

/* ================================================================
 *  Wire subtype constants
 * ================================================================ */

#define BC_SUB_RRR  (0u << 6)
#define BC_SUB_RRI  (1u << 6)
#define BC_SUB_RRS  (2u << 6)
#define BC_SUB_VAR  (3u << 6)

/* emit a full 8-byte fixed instruction */
static void bc_emit_fixed(bc_buf *out, uint8_t subtype, uint8_t opnum,
                          uint8_t r1, uint8_t r2, uint8_t r3, uint32_t imm) {
	bc_buf_u8(out, subtype | opnum);
	bc_buf_u8(out, r1);
	bc_buf_u8(out, r2);
	bc_buf_u8(out, r3);
	bc_buf_u32(out, imm);
}

/* emit VAR header (8 bytes); caller appends nwords*4 bytes of payload */
static void bc_emit_var_header(bc_buf *out, uint8_t opnum,
                               uint8_t r1, uint8_t r2, uint8_t nwords,
                               uint32_t imm) {
	bc_buf_u8(out, BC_SUB_VAR | opnum);
	bc_buf_u8(out, r1);
	bc_buf_u8(out, r2);
	bc_buf_u8(out, nwords);
	bc_buf_u32(out, imm);
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

/* ================================================================
 *  Label map: label name -> absolute opcode index
 * ================================================================ */

static void bc_label_set(ci_map *m, char *name, uint32_t word_idx) {
	ci_map_set_str(m, name, (ci_ptr)(uintptr_t)(word_idx + 1));
}

static uint32_t bc_label_get(ci_map *m, const char *name) {
	ci_ptr v = ci_map_get_str(m, name);
	if (!v) b_error("encoder: undefined label '%s'", name);
	return (uint32_t)(uintptr_t)(v - 1);
}

/* ================================================================
 *  Function encoder (single emit pass + label backpatch)
 * ================================================================ */

static bc_buf *be_encode_function(b_function *f) {
	bc_buf  *out    = bc_buf_new();
	ci_map  *labels = ci_map_new(16);
	uint32_t total  = ci_arr_len(f->bytecode);
	uint32_t ic     = 0;   /* instruction (opcode) counter — VM indexes by this */

	/* emit pass: encode all instructions, record labels, mark jumps */
	for (uint32_t i = 0; i < total; i++) {
		b_opcode *op = (b_opcode *)ci_arr_index(f->bytecode, i);

		if (op->op == B_LABEL) {
			bc_label_set(labels, op->r.dst->value.label, ic);
			continue;
		}

		/* --- fixed encodings --- */

		if (op->enc == B_ENC_RRR) {
			bc_emit_fixed(out, BC_SUB_RRR, op->op,
				op->rrr.dst->number,
				op->rrr.src1->number,
				op->rrr.src2->number, 0);
		}
		else if (op->enc == B_ENC_RRI && op->op == B_ITERSTEP) {
			/* ITERSTEP: [first_reg][nregs][0][i32 jump_target] */
			uint32_t imm_off = bc_buf_len(out) + 4;
			bc_emit_fixed(out, BC_SUB_RRI, op->op,
				op->rri32.dst->number,
				(uint8_t)op->rri32.imm,
				0, 0);
			op->rri32.src1->label_patch_addr = (uint8_t *)(uintptr_t)imm_off;
		}
		else if (op->enc == B_ENC_RRI) {
			bc_emit_fixed(out, BC_SUB_RRI, op->op,
				op->rri32.dst->number,
				op->rri32.src1->number,
				0, op->rri32.imm);
		}
		else if (op->enc == B_ENC_RRS) {
			bc_emit_fixed(out, BC_SUB_RRS, op->op,
				op->rri32.dst->number,
				op->rri32.src1->number,
				0, (uint32_t)op->rri32.imm);
		}
		else if (op->enc == B_ENC_RI) {
			int64_t imm = op->rri32.imm;
			if (imm >= INT32_MIN && imm <= INT32_MAX) {
				bc_emit_fixed(out, BC_SUB_RRI, op->op,
					op->rri32.dst->number,
					op->rri32.dst->number,
					0, (uint32_t)imm);
			} else {
				if (op->op != B_LOADINT)
					b_error("64-bit imm supported only on LOADINT");
				uint64_t u = (uint64_t)imm;
				bc_emit_var_header(out, op->op,
					op->rri32.dst->number, 0, 2, 0);
				bc_buf_u32(out, (uint32_t)u);
				bc_buf_u32(out, (uint32_t)(u >> 32));
			}
		}
		else if (op->enc == B_ENC_RD) {
			uint64_t u;
			memcpy(&u, &op->rd.imm, sizeof(double));
			bc_emit_var_header(out, op->op,
				op->rd.dst->number, 0, 2, 0);
			bc_buf_u32(out, (uint32_t)u);
			bc_buf_u32(out, (uint32_t)(u >> 32));
		}
		else if (op->enc == B_ENC_R) {
			if (op->op == B_JMPF || op->op == B_JMPT) {
				/* emit with placeholder imm=0, record patch offset */
				uint32_t imm_off = bc_buf_len(out) + 4;
				bc_emit_fixed(out, BC_SUB_RRI, op->op,
					op->r.dst->number, 0, 0, 0);
				op->r.src->label_patch_addr = (uint8_t *)(uintptr_t)imm_off;
			} else {
				bc_emit_fixed(out, BC_SUB_RRR, op->op,
					op->r.dst->number, op->r.src->number, 0, 0);
			}
		}
		else if (op->enc == B_ENC_R0) {
			if (op->op == B_JMP) {
				uint32_t imm_off = bc_buf_len(out) + 4;
				bc_emit_fixed(out, BC_SUB_RRI, op->op, 0, 0, 0, 0);
				op->r.dst->label_patch_addr = (uint8_t *)(uintptr_t)imm_off;
			} else {
				bc_emit_fixed(out, BC_SUB_RRR, op->op,
					op->r.dst->number, 0, 0, 0);
			}
		}
		else if (op->enc == B_ENC_CALL) {
			bc_emit_fixed(out, BC_SUB_RRR, op->op,
				op->call.base->number,
				(uint8_t)op->call.nargs,
				(uint8_t)op->call.nrets, 0);
		}
		else if (op->enc == B_ENC_DECIDE) {
			b_error("encoder: unresolved DECIDE for '%s'", b_op_names[op->op]);
		}

		/* --- VAR encodings --- */

		else if (op->enc == B_ENC_VAR || op->enc == B_ENC_VAR_STRID) {
			uint32_t vcnt = ci_arr_len(op->var.regs);

			if (op->op == B_RETURN || op->op == B_LOADNULL) {
				uint32_t nwords = (vcnt + 3) / 4;
				if (nwords > 255) b_error("%s: too many registers", b_op_names[op->op]);
				bc_emit_var_header(out, op->op, (uint8_t)vcnt, 0, (uint8_t)nwords, 0);
				for (uint32_t v = 0; v < vcnt; v++)
					bc_buf_u8(out, ((b_reg)ci_arr_index(op->var.regs, v))->number);
				bc_buf_pad4(out, nwords, vcnt);
			}
			else if (op->op == B_MOVETO || op->op == B_MOVEFROM) {
				uint8_t base = op->r.dst->number;
				uint32_t nwords = (vcnt + 3) / 4;
				if (nwords > 255) b_error("%s: too many registers", b_op_names[op->op]);
				bc_emit_var_header(out, op->op, base, (uint8_t)vcnt, (uint8_t)nwords, 0);
				for (uint32_t v = 0; v < vcnt; v++)
					bc_buf_u8(out, ((b_reg)ci_arr_index(op->var.regs, v))->number);
				bc_buf_pad4(out, nwords, vcnt);
			}
			else if (op->op == B_NEWARRAY) {
				if (vcnt < 1) b_error("NEWARRAY: missing dst");
				b_reg dst = (b_reg)ci_arr_index(op->var.regs, 0);
				uint32_t nelem = vcnt - 1;
				uint32_t nwords = (nelem + 3) / 4;
				if (nwords > 255) b_error("NEWARRAY: too many elements");
				bc_emit_var_header(out, op->op,
					dst->number, (uint8_t)nelem, (uint8_t)nwords, 0);
				for (uint32_t v = 1; v < vcnt; v++)
					bc_buf_u8(out, ((b_reg)ci_arr_index(op->var.regs, v))->number);
				bc_buf_pad4(out, nwords, nelem);
			}
			else if (op->op == B_NEWMAP) {
				if (vcnt < 1) b_error("NEWMAP: missing dst");
				b_reg dst = (b_reg)ci_arr_index(op->var.regs, 0);
				if ((vcnt - 1) % 2 != 0) b_error("NEWMAP: need pairs of [val, key]");
				uint32_t npairs = (vcnt - 1) / 2;
				if (npairs > 255) b_error("NEWMAP: too many pairs");
				bc_emit_var_header(out, op->op,
					dst->number, (uint8_t)npairs, (uint8_t)npairs, 0);
				for (uint32_t p = 0; p < npairs; p++) {
					b_reg val = (b_reg)ci_arr_index(op->var.regs, 1 + p * 2);
					b_reg key = (b_reg)ci_arr_index(op->var.regs, 1 + p * 2 + 1);
					if (key->type == B_REG_STRING) {
						bc_buf_u8(out, 0);
						bc_buf_u8(out, val->number);
						bc_buf_u16(out, (uint16_t)key->interned_string_id);
					} else {
						bc_buf_u8(out, 1);
						bc_buf_u8(out, val->number);
						bc_buf_u8(out, key->number);
						bc_buf_u8(out, 0);
					}
				}
			}
			else if (op->op == B_HASHACCESS) {
				if (vcnt < 2) b_error("HASHACCESS: need dst + src");
				b_reg dst = (b_reg)ci_arr_index(op->var.regs, 0);
				b_reg src = (b_reg)ci_arr_index(op->var.regs, 1);
				uint32_t nkeys = vcnt - 2;
				uint32_t nwords = (nkeys + 1) / 2 + 1;
				if (nwords > 255) b_error("HASHACCESS: too many keys");
				bc_emit_var_header(out, op->op,
					dst->number, src->number, (uint8_t)nwords, 0);
				for (uint32_t k = 0; k < nkeys; k++) {
					b_reg key = (b_reg)ci_arr_index(op->var.regs, 2 + k);
					if (key->type != B_REG_STRING)
						b_error("HASHACCESS: key must be a string id");
					bc_buf_u16(out, (uint16_t)key->interned_string_id);
				}
				if (nkeys % 2 != 0) bc_buf_u16(out, 0);
				
				bc_buf_u16(out, 0);
				bc_buf_u16(out, 0);
			}
			else {
				b_error("encoder: unhandled VAR op '%s'", b_op_names[op->op]);
			}
		}
		else {
			b_error("encoder: unknown enc %u for '%s'", op->enc, b_op_names[op->op]);
		}

		ic++;
	}

	/* backpatch pass: resolve jump targets from actual label positions */
	for (uint32_t i = 0; i < total; i++) {
		b_opcode *op = (b_opcode *)ci_arr_index(f->bytecode, i);
		b_reg label_reg = NULL;

		if (op->op == B_JMP && op->enc == B_ENC_R0)
			label_reg = op->r.dst;
		else if ((op->op == B_JMPF || op->op == B_JMPT) && op->enc == B_ENC_R)
			label_reg = op->r.src;
		else if (op->op == B_ITERSTEP && op->enc == B_ENC_RRI)
			label_reg = op->rri32.src1;

		if (label_reg && label_reg->label_patch_addr) {
			uint32_t imm_off = (uint32_t)(uintptr_t)label_reg->label_patch_addr;
			uint32_t target = bc_label_get(labels, label_reg->value.label);
			bc_buf_patch_u32(out, imm_off, target);
			label_reg->label_patch_addr = NULL;
		}
	}

	return out;
}

/* ================================================================
 *  Unit binary writer
 * ================================================================ */

static bc_buf *be_encode_unit(b_unit *unit) {
	uint32_t fcnt = ci_arr_len(unit->functions);

	/* string table: code strings first, then function names */
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
	bc_buf **fn_codes = b_malloc(fcnt * sizeof(bc_buf *));
	for (uint32_t i = 0; i < fcnt; i++) {
		b_function *f = (b_function *)ci_arr_index(unit->functions, i);
		fn_codes[i] = be_encode_function(f);
	}

	bc_buf *out = bc_buf_new();

	/* file header */
	bc_buf_bytes(out, "CIBC", 4);
	bc_buf_u16(out, 2);                        /* version 2 */
	bc_buf_u16(out, (uint16_t)strtab->count);
	bc_buf_u16(out, (uint16_t)fcnt);

	/* string table */
	for (uint32_t i = 0; i < strtab->count; i++) {
		bc_buf_u16(out, (uint16_t)strtab->lens[i]);
		bc_buf_bytes(out, strtab->strs[i], strtab->lens[i]);
	}

	/* functions */
	for (uint32_t i = 0; i < fcnt; i++) {
		b_function  *f  = (b_function *)ci_arr_index(unit->functions, i);
		bc_buf      *fc = fn_codes[i];
		uint16_t name_idx  = bc_strtab_intern(strtab, f->name, (uint32_t)strlen(f->name));
		uint8_t  reg_count = f->cb ? f->cb->reg_next : 0;

		bc_buf_u16(out, name_idx);
		bc_buf_u8 (out, f->arg_count);
		bc_buf_u8 (out, reg_count);
		bc_buf_u16(out, 0);          /* local_count placeholder */
		bc_buf_u32(out, bc_buf_len(fc));   /* code_bytes */
		bc_buf_bytes(out, bc_buf_data(fc), bc_buf_len(fc));
	}

	for (uint32_t i = 0; i < fcnt; i++)
		bc_buf_free(fn_codes[i]);
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
	if (r->pos + 1 > r->len) b_error("decode: unexpected end");
	return r->data[r->pos++];
}
static uint16_t bcr_u16(bc_reader *r) {
	uint16_t v = bcr_u8(r); return v | ((uint16_t)bcr_u8(r) << 8);
}
static uint32_t bcr_u32(bc_reader *r) {
	uint32_t v = bcr_u16(r); return v | ((uint32_t)bcr_u16(r) << 16);
}
static void bcr_skip(bc_reader *r, uint32_t n) {
	if (r->pos + n > r->len) b_error("decode: unexpected end");
	r->pos += n;
}

static void bc_dump(const uint8_t *data, uint32_t len,
                    char **strs, uint16_t str_cnt) {
	bc_reader rr = { data, 0, len };
	bc_reader *r = &rr;

	/* file header */
	char magic[5] = {0};
	for (int i = 0; i < 4; i++) magic[i] = (char)bcr_u8(r);
	uint16_t version  = bcr_u16(r);
	uint16_t sc       = bcr_u16(r);
	uint16_t fn_cnt   = bcr_u16(r);

	printf("magic=%.4s  version=%u  strings=%u  functions=%u\n",
	       magic, version, sc, fn_cnt);

	/* string table */
	if (!strs) {
		strs = b_malloc(sc * sizeof(char *));
		str_cnt = sc;
		printf("\n--- strings (%u) ---\n", sc);
		for (uint16_t i = 0; i < sc; i++) {
			uint16_t slen = bcr_u16(r);
			strs[i] = b_malloc((uint32_t)slen + 1);
			for (uint16_t c = 0; c < slen; c++) strs[i][c] = (char)bcr_u8(r);
			strs[i][slen] = '\0';
			printf("  s%-3u  \"%s\"\n", i, strs[i]);
		}
	} else {
		for (uint16_t i = 0; i < sc; i++) {
			uint16_t slen = bcr_u16(r);
			bcr_skip(r, slen);
		}
	}

	/* functions */
	for (uint16_t fi = 0; fi < fn_cnt; fi++) {
		uint16_t name_idx  = bcr_u16(r);
		uint8_t  arg_count = bcr_u8(r);
		uint8_t  reg_count = bcr_u8(r);
		uint16_t loc_count = bcr_u16(r);
		uint32_t code_bytes = bcr_u32(r);

		(void)arg_count;
		(void)loc_count;

		const char *fname = (name_idx < str_cnt) ? strs[name_idx] : "?";
		printf("\n--- fn[%u] \"%s\"  args=%u  regs=%u  bytes=%u ---\n",
		       fi, fname, arg_count, reg_count, code_bytes);

		uint32_t bytes_read = 0;
		while (bytes_read < code_bytes) {
			uint8_t  b0      = bcr_u8(r);
			uint8_t  sub     = b0 >> 6;
			uint8_t  opnum   = b0 & 0x3F;
			uint8_t  r1      = bcr_u8(r);
			uint8_t  r2      = bcr_u8(r);
			uint8_t  r3      = bcr_u8(r);
			uint32_t imm     = bcr_u32(r);
			bytes_read += 8;

			uint32_t word_pos = (bytes_read - 8) / 4;
			const char *oname = (opnum < B_OP_COUNT) ? b_op_names[opnum] : "???";
			const char *sub_names[] = { "RRR", "RRI", "RRS", "VAR" };

			printf("  [%3u] %-12s %-3s  ", word_pos, oname, sub_names[sub]);

			switch (sub) {
			case 0: /* RRR */
				printf("r%u, r%u, r%u", r1, r2, r3);
				break;
			case 1: /* RRI */
				if (opnum == B_JMP) {
					printf("[%u]", imm);
				} else if (opnum == B_JMPF || opnum == B_JMPT) {
					printf("r%u, [%u]", r1, imm);
				} else {
					printf("r%u, %d", r1, (int32_t)imm);
				}
				break;
			case 2: /* RRS */
				if (imm < str_cnt)
					printf("r%u, s%u  // \"%s\"", r1, imm, strs[imm]);
				else
					printf("r%u, s%u", r1, imm);
				break;
			case 3: { /* VAR */
				uint8_t  payload[1024];
				uint32_t nwords = r3;
				uint32_t pb     = nwords * 4;
				for (uint32_t k = 0; k < pb; k++) payload[k] = bcr_u8(r);
				bytes_read += pb;

				if (opnum == B_CALL) {
					uint32_t nargs = r1, nrets = r2, fn = imm;
					uint32_t self = pb > 0 ? payload[0] : 0;
					printf("r%u, %u, %u  // fn, nargs, nrets", fn, nargs, nrets);
					printf("  ;  self=r%u", self);
					if (nargs) {
						printf(" [");
						for (uint32_t a = 0; a < nargs; a++)
							printf("%sr%u", a ? "," : "", payload[1 + a]);
						printf("]");
					}
					if (nrets) {
						printf(" -> [");
						for (uint32_t a = 0; a < nrets; a++)
							printf("%sr%u", a ? "," : "", payload[1 + nargs + a]);
						printf("]");
					}
				} else if (opnum == B_RETURN || opnum == B_LOADNULL) {
					uint32_t nregs = r1;
					printf("[");
					for (uint32_t k = 0; k < nregs; k++)
						printf("%sr%u", k ? "," : "", payload[k]);
					printf("]  // %s", b_op_names[opnum]);
				} else if (opnum == B_NEWARRAY) {
					uint32_t dst = r1, nelem = r2;
					printf("r%u = [", dst);
					for (uint32_t k = 0; k < nelem; k++)
						printf("%sr%u", k ? "," : "", payload[k]);
					printf("]");
				} else if (opnum == B_NEWMAP) {
					uint32_t dst = r1, npairs = r2;
					printf("r%u = {", dst);
					for (uint32_t p = 0; p < npairs; p++) {
						uint8_t  type = payload[p * 4 + 0];
						uint8_t  val  = payload[p * 4 + 1];
						uint16_t key  = (uint16_t)payload[p * 4 + 2]
						              | ((uint16_t)payload[p * 4 + 3] << 8);
						printf("%s", p ? ", " : "");
						if (type == 0) {
							const char *ks = (key < str_cnt) ? strs[key] : "?";
							printf("\"%s\": r%u", ks, val);
						} else {
							printf("r%u: r%u", key, val);
						}
					}
					printf("}");
				} else if (opnum == B_HASHACCESS) {
					uint32_t dst = r1, src = r2;
					uint32_t nkeys = nwords * 2;
					printf("r%u = r%u", dst, src);
					for (uint32_t k = 0; k < nkeys; k++) {
						uint16_t sid = (uint16_t)payload[k * 2]
						             | ((uint16_t)payload[k * 2 + 1] << 8);
						if (sid == 0) break;
						const char *ks = (sid < str_cnt) ? strs[sid] : "?";
						printf("[\"%s\"]", ks);
					}
				} else if (opnum == B_LOADDOUBLE) {
					double d;
					memcpy(&d, payload, sizeof(double));
					printf("r%u, %g", r1, d);
				} else if (opnum == B_LOADINT) {
					uint64_t u = (uint32_t)payload[0]
					           | ((uint64_t)payload[1] << 8)
					           | ((uint64_t)payload[2] << 16)
					           | ((uint64_t)payload[3] << 24)
					           | ((uint64_t)payload[4] << 32)
					           | ((uint64_t)payload[5] << 40)
					           | ((uint64_t)payload[6] << 48)
					           | ((uint64_t)payload[7] << 56);
					printf("r%u, %lld", r1, (long long)(int64_t)u);
				} else {
					printf("r%u, r%u  // +%u words", r1, r2, nwords);
					for (uint32_t w = 0; w < nwords; w += 2) {
						uint32_t a = ((uint32_t)payload[w*4+0]) | ((uint32_t)payload[w*4+1]<<8)
						           | ((uint32_t)payload[w*4+2]<<16) | ((uint32_t)payload[w*4+3]<<24);
						printf("  %08x", a);
					}
				}
				break;
			}
			}
			printf("\n");
		}
	}
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
		printf("\n=== Binary (%u bytes) ===\n", bc_buf_len(binary));

		/* write .cbc */
		char outpath[4096];
		snprintf(outpath, sizeof(outpath), "%s.cbc", argv[i]);
		FILE *fp = fopen(outpath, "wb");
		if (fp) {
			fwrite(bc_buf_data(binary), 1, bc_buf_len(binary), fp);
			fclose(fp);
			printf("written: %s\n", outpath);
		} else {
			fprintf(stderr, "warning: cannot write '%s'\n", outpath);
		}

		/* dump decoded listing */
		printf("\n=== Decoded ===\n");
		bc_dump(bc_buf_data(binary), bc_buf_len(binary), NULL, 0);

		bc_buf_free(binary);
		ast_node_free(block);
		ast_free(a);
		b_parser_free(p);
	}

	ci_shutdown();
	return 0;
}
#endif /* ENCODER_NO_MAIN */
