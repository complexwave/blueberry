/* ================================================================
 *  Bytecode reader helpers
 * ================================================================ */

typedef struct {
	const uint8_t *data;
	uint32_t pos;
	uint32_t len;
} bb_reader;

static uint8_t bb_read_u8(bb_reader *r) {
	if (r->pos >= r->len)
		bb_error("load: unexpected end of file");
	return r->data[r->pos++];
}

static uint16_t bb_read_u16(bb_reader *r) {
	uint16_t lo = bb_read_u8(r);
	return lo | ((uint16_t)bb_read_u8(r) << 8);
}

static void bb_read_bytes(bb_reader *r, void *dst, uint32_t n) {
	if (r->pos + n > r->len)
		bb_error("load: unexpected end of file");
	memcpy(dst, r->data + r->pos, n);
	r->pos += n;
}


/* ================================================================
 *  Bytecode loader
 *
 *  .cbc file format (little-endian):
 *    "CIBC"              4 B  magic
 *    version             u16  = 1
 *    string_count        u16
 *    function_count      u16
 *    --- string table ---
 *    [len u16][bytes]  × string_count          (no null terminator)
 *    --- functions ---
 *    [name_idx u16][arg_count u8][reg_count u8]
 *    [local_count u16]                          (0 for now)
 *    [op_count u16]
 *    [...binary opcodes: op_count × 4 bytes...]
 *
 *  Opcode wire format — first byte: [subtype:2][opnum:6]
 *    subtype 00  RRR    [op][dst][src1][src2]   4 B
 *    subtype 01  RRU8   [op][dst][src1][imm8]   4 B
 *    subtype 10  RU16   [op][dst][imm_lo][imm_hi] 4 B (LE)
 *    subtype 11  VAR    [op][b1][extra_words][0] + extra_words × 4 B
 * ================================================================ */

static bb_unit *bb_vm_loadbytecode(bb_vm *vm, const uint8_t *buf, uint32_t len) {
	bb_reader rr = { buf, 0, len };
	bb_reader *r = &rr;

	/* header */
	uint8_t m0 = bb_read_u8(r);
	uint8_t m1 = bb_read_u8(r);
	uint8_t m2 = bb_read_u8(r);
	uint8_t m3 = bb_read_u8(r);
	if (m0 != 'C' || m1 != 'I' || m2 != 'B' || m3 != 'C')
		bb_error("load: bad magic (expected CIBC)");

	uint16_t version  = bb_read_u16(r);
	if (version != 1)
		bb_error("load: unsupported version %u", version);

	uint16_t str_count = bb_read_u16(r);
	uint16_t fn_count  = bb_read_u16(r);

	/* allocate unit with flex array for str2intern */
	bb_unit *unit = b_malloc(sizeof(bb_unit) + str_count * sizeof(ci_ptr));
	unit->str_count = str_count;
	unit->functions = ci_arr_new(fn_count ? fn_count : 1);

	/* string table */
	for (uint16_t i = 0; i < str_count; i++) {
		uint16_t slen = bb_read_u16(r);
		char *tmp = b_malloc(slen + 1);
		bb_read_bytes(r, tmp, slen);
		tmp[slen] = '\0';
		unit->str2intern[i] = bb_vm_istring(vm, tmp, slen);
		free(tmp);
	}

	/* functions */
	for (uint16_t i = 0; i < fn_count; i++) {
		uint16_t name_idx  = bb_read_u16(r);
		uint8_t  arg_count = bb_read_u8(r);
		uint8_t  reg_count = bb_read_u8(r);
		uint16_t loc_count = bb_read_u16(r);

		/* skip locals (not used yet) */
		for (uint16_t li = 0; li < loc_count; li++) {
			bb_read_u16(r);
			bb_read_u8(r);
		}

		uint16_t op_count  = bb_read_u16(r);
		uint32_t code_bytes = (uint32_t)op_count * 4;

		bb_function *fn = b_malloc(sizeof(bb_function));
		fn->unit        = unit;
		fn->name        = (name_idx < str_count) ? unit->str2intern[name_idx] : NULL;
		fn->flags       = 0;
		fn->args        = arg_count;
		fn->regs        = reg_count;
		fn->code_length = op_count;
		fn->code        = b_malloc(code_bytes ? code_bytes : 1);
		bb_read_bytes(r, fn->code, code_bytes);

		ci_arr_push(unit->functions, fn);
	}

	ci_arr_push(vm->units, unit);
	return unit;
} 
