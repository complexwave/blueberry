
static void bb_dump_function(bb_function *fn, uint32_t stop_at) {
	uint8_t  *code  = fn->code;
	uint32_t  bi    = 0;   /* byte cursor */

	while (bi < fn->code_length && bi / 4 < stop_at) {
		uint32_t wi      = bi / 4;  /* word index (4-byte words) */
		uint8_t  b0      = code[bi];
		uint8_t  b1      = code[bi+1];
		uint8_t  b2      = code[bi+2];
		uint8_t  b3      = code[bi+3];
		uint32_t imm     = (uint32_t)code[bi+4] | ((uint32_t)code[bi+5]<<8)
		                 | ((uint32_t)code[bi+6]<<16) | ((uint32_t)code[bi+7]<<24);
		uint8_t  subtype = b0 >> 6;
		uint8_t  opnum   = b0 & 0x3F;
		const char *oname = (opnum < B_OP_COUNT) ? b_op_names[opnum] : "???";

		printf("    [%3u] %-12s", wi, oname);
		bi += 8;

		switch (subtype) {
		case 0: /* RRR */
			printf("r%u, r%u, r%u", b1, b2, b3);
			break;
		case 1: /* RRI */
			if (opnum == B_JMP)
				printf("[%u]", imm);
			else if (opnum == B_JMPF || opnum == B_JMPT)
				printf("r%u, [%u]", b1, imm);
			else if (opnum == B_ITERSTEP)
				printf("r%u, %u, [%u]", b1, b2, imm);
			else
				printf("r%u, %d", b1, (int32_t)imm);
			break;
		case 2: /* RRS */
			printf("r%u, s%u", b1, imm);
			break;
		case 3: { /* VAR */
			uint8_t nwords = b3;
			printf("r%u, r%u  +%u", b1, b2, nwords);
			for (uint8_t w = 0; w < nwords; w++) {
				uint32_t word = (uint32_t)code[bi] | ((uint32_t)code[bi+1]<<8)
				              | ((uint32_t)code[bi+2]<<16) | ((uint32_t)code[bi+3]<<24);
				printf("\n    [%3u]             %08x", bi / 4, word);
				bi += 4;
			}
			break;
		}
		}
		printf("\n");
	}
}

static void bb_dump_unit(bb_unit *unit) {
	uint32_t fcnt = ci_arr_len(unit->functions);
	printf("--- unit: %u strings, %u functions ---\n", unit->str_count, fcnt);

	if (unit->str_count) {
		printf("\n  strings:\n");
		for (uint16_t i = 0; i < unit->str_count; i++) {
			ci_ptr s = unit->str2intern[i];
			if (s && CI_IS_ANY_STR(s)) {
				printf("    [%u] \"%.*s\"\n", i,
				       (int)ci_str_len(s), (char *)ci_str_head(s));
			} else {
				printf("    [%u] <%p>\n", i, s);
			}
		}
	}

	printf("\n  functions:\n");
	for (uint32_t i = 0; i < fcnt; i++) {
		bb_function *fn = (bb_function *)ci_arr_index(unit->functions, i);
		if (fn->name && CI_IS_ANY_STR(fn->name)) {
			printf("  [%u] fn '%.*s'  args=%u regs=%u bytes=%u\n", i,
			       (int)ci_str_len(fn->name), (char *)ci_str_head(fn->name),
			       fn->args, fn->regs, fn->code_length);
		} else {
			printf("  [%u] fn <unnamed>  args=%u regs=%u bytes=%u\n", i,
			       fn->args, fn->regs, fn->code_length);
		}
		bb_dump_function(fn, UINT32_MAX);
	}
}

static void bb_dump_frame(bb_coro *c, uint32_t depth, bb_frame *frame, int dumpregs) {
	bb_function *fn = bb_coro_frame_function(frame);
	uint32_t base  = frame->stack_base;
	uint32_t nregs = fn->regs;

	/* Calculate PC offset relative to function's ops */
	uint32_t pc_offset = 0;
	if (frame->pc) {
		bb_cached_op *ops = bb_function_ops(fn);
		if (ops)
			pc_offset = (uint32_t)(frame->pc - ops);
	}

	/* Function name and metadata */
	printf("  [%u] ", depth);
	if (fn->name && CI_IS_ANY_STR(fn->name)) {
		printf("fn '%.*s'", (int)ci_str_len(fn->name), (char *)ci_str_head(fn->name));
	} else {
		printf("fn <unnamed>");
	}
	printf("  pc=[%u]  regs=%u\n", pc_offset, nregs);

	if (!dumpregs)
		return;

	/* Register values */
	for (uint32_t i = 0; i < nregs; i++) {
		ci_ptr val = c->stack->data[base + i];
		printf("      R(%u) = ", i);
		if (!val) {
			printf("null");
		} else if (CI_IS_INT(val)) {
			printf("%ld", (long)CI_INT(val));
		} else if (CI_IS_BOOL(val)) {
			printf("%s", val == CI_BOOL(1) ? "true" : "false");
		} else if (CI_IS_ANY_STR(val)) {
			printf("\"%.*s\"", (int)ci_str_len(val), (char *)ci_str_head(val));
		} else {
			printf("<%p>", (void *)val);
		}
		printf("\n");
	}
}

static void bb_dump_regs(bb_coro *c) {
	if (c->fstack_pos == 0)
		return;

	bb_frame *frame = bb_coro_frame_top(c);
	uint32_t base  = frame->stack_base;
	uint32_t nregs = bb_coro_frame_function(frame)->regs;

	printf("\n--- registers (%u) ---\n", nregs);
	for (uint32_t i = 0; i < nregs; i++) {
		ci_ptr val = c->stack->data[base + i];
		printf("  R(%u) = ", i);
		if (!val) {
			printf("null");
		} else if (CI_IS_INT(val)) {
			printf("%ld", (long)CI_INT(val));
		} else {
			printf("<%p>", val);
		}
		printf("\n");
	}
}

static void bb_coro_dump_stack(bb_coro *c, int dumpregs) {
	if (c->fstack_pos == 0) {
		printf("\n--- call stack (empty) ---\n");
		return;
	}

	printf("\n--- call stack (%u frames) ---\n", c->fstack_pos);
	for (uint32_t i = 0; i < c->fstack_pos; i++) {
		bb_frame *frame = bb_coro_frame(c, i);
		bb_dump_frame(c, i, frame, dumpregs);
	}
} 
