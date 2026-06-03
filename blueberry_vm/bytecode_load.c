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

static uint32_t bb_read_u32(bb_reader *r) {
	uint32_t lo = bb_read_u16(r);
	return lo | ((uint32_t)bb_read_u16(r) << 16);
}

static void bb_read_bytes(bb_reader *r, void *dst, uint32_t n) {
	if (r->pos + n > r->len)
		bb_error("load: unexpected end of file");
	memcpy(dst, r->data + r->pos, n);
	r->pos += n;
}

static void bb_unit_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	bb_unit *unit = (bb_unit *)ptr;
	
	ci_dec(unit->functions);
}

static void bb_function_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	bb_function *fn = (bb_function *)ptr;
	
	free(fn->code);
}


/* ================================================================
 *  Bytecode loader
 *
 *  .cbc file format v2 (little-endian):
 *    "CIBC"              4 B  magic
 *    version             u16  = 2
 *    string_count        u16
 *    function_count      u16
 *    --- string table ---
 *    [len u16][bytes]  × string_count          (no null terminator)
 *    --- functions ---
 *    [name_idx u16][arg_count u8][reg_count u8]
 *    [local_count u16]                          (0 for now)
 *    [code_bytes u32]
 *    [...binary opcodes: code_bytes bytes...]
 *
 *  Opcode wire format — every instruction is 8 bytes:
 *    [op:u8][r1:u8][r2:u8][r3:u8][imm:i32-LE]
 *    top 2 bits of op = subtype:
 *    subtype 00  RRR    r1,r2,r3 are regs; imm=0
 *    subtype 01  RRI    r1,r2,r3 are regs; imm=signed integer
 *    subtype 10  RRS    r1 is reg; imm=string table id
 *    subtype 11  VAR    r1,r2 per-op; r3=payload word count; imm varies
 *                       followed by r3 × 4-byte payload words
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
	if (version != 2)
		bb_error("load: unsupported version %u (expected 2)", version);

	uint16_t str_count = bb_read_u16(r);
	uint16_t fn_count  = bb_read_u16(r);

	/* allocate unit with flex array for str2intern */
	bb_unit *unit = CI_MALLOC_OBJ(sizeof(bb_unit) + str_count * sizeof(ci_ptr));
	unit->gc.destructor = bb_unit_destructor;
	
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

		uint32_t code_bytes = bb_read_u32(r);

		bb_function *fn = CI_MALLOC_OBJ(sizeof(bb_function));
		fn->gc.destructor = bb_function_destructor;
		
		fn->unit        = unit;
		fn->name        = (name_idx < str_count) ? unit->str2intern[name_idx] : NULL;
		fn->flags       = 0;
		fn->args        = arg_count;
		fn->regs        = reg_count;
		fn->code_length = code_bytes;   /* bytes; word count derived in bb_build_cached */
		fn->code        = b_malloc(code_bytes ? code_bytes : 1);
		bb_read_bytes(r, fn->code, code_bytes);

		ci_arr_push(unit->functions, fn);
	}

	ci_arr_push(vm->units, unit);
	return unit;
} 
