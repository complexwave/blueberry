
static void bb_dump_function(bb_function *fn, uint32_t stop_at) {
	uint8_t *code = fn->code;
	uint32_t word_idx = 0;

	while (word_idx < fn->code_length && word_idx < stop_at) {
		uint32_t off = word_idx * 4;
		uint8_t b0 = code[off];
		uint8_t subtype = b0 >> 6;
		uint8_t opnum = b0 & 0x3F;
		const char *oname = (opnum < B_OP_COUNT) ? b_op_names[opnum] : "???";

		printf("    [%3u] %-12s", word_idx, oname);
		word_idx++;

		switch (subtype) {
		case 0:
			printf("%u, %u, %u", code[off+1], code[off+2], code[off+3]);
			break;
		case 1:
			printf("%u, %u, [%u]", code[off+1], code[off+2], code[off+3]);
			break;
		case 2: {
			uint16_t imm = code[off+2] | ((uint16_t)code[off+3] << 8);
			if (opnum == B_JMP || opnum == B_JMPF || opnum == B_JMPT) {
				int32_t rel = (int32_t)imm - (int32_t)(word_idx - 1);
				printf("%u, %u [%+d]", code[off+1], imm, rel);
			} else {
				printf("%u, [%u]", code[off+1], imm);
			}
			break;
		}
		case 3: {
			uint8_t b1 = code[off+1];
			uint8_t extra = code[off+2];
			uint8_t b3 = code[off+3];
			printf("%u, [%u], %u", b1, extra, b3);
			for (uint8_t w = 0; w < extra; w++) {
				uint32_t poff = (word_idx + w) * 4;
				if (w == 0)
					printf("\n    [%3u]           ( ", word_idx + w);
				else
					printf("\n    [%3u]             ", word_idx + w);
				for (int k = 0; k < 4; k++) {
					int last = (w == extra - 1) && (k == 3);
					printf("%u%s", code[poff + k], last ? " )" : ", ");
				}
			}
			word_idx += extra;
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
			printf("  [%u] fn '%.*s'  args=%u regs=%u ops=%u\n", i,
			       (int)ci_str_len(fn->name), (char *)ci_str_head(fn->name),
			       fn->args, fn->regs, fn->code_length);
		} else {
			printf("  [%u] fn <unnamed>  args=%u regs=%u ops=%u\n", i,
			       fn->args, fn->regs, fn->code_length);
		}
		bb_dump_function(fn, UINT32_MAX);
	}
}

static void bb_dump_regs(bb_coro *c) {
	if (c->fstack_pos == 0)
		return;

	bb_frame *frame = &c->fstack[c->fstack_pos - 1];
	uint32_t base  = frame->stack_base;
	uint32_t nregs = frame->function->regs;

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
