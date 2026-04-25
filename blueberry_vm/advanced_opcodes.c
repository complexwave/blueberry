
/* ================================================================
 *  Handler types for ri16 and var tables
 * ================================================================ */

typedef void (*bb_ri16_fn)(bb_coro *c, ci_ptr *slot, uint16_t imm);
typedef void (*bb_var_fn)(bb_coro *c, ci_ptr *stack, uint8_t *op);

/* ================================================================
 *  RRR ops  —  ci_ptr fn(vm, a, b)
 * ================================================================ */

static inline ci_ptr bb_op_move(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)b;
	return a;
}

static inline ci_ptr bb_op_hashaccess_rrr(bb_coro *c, ci_ptr a, ci_ptr b) {
	return bb_proto_find(c->vm, a, b);
}

static inline ci_ptr bb_op_loadtrue(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)a; (void)b;
	return (ci_ptr)CI_BOOL(1);
}

static inline ci_ptr bb_op_loadfalse(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)a; (void)b;
	return (ci_ptr)CI_BOOL(0);
}

static inline ci_ptr bb_op_mapaccess(bb_coro *c, ci_ptr map, ci_ptr key) {
	(void)c;
	if (!CI_IS_MAP(map))
		bb_error("MAPACCESS: operand is not a MAP");
	return ci_map_find(map, key);
}

static inline ci_ptr bb_op_methodbind(bb_coro *c, ci_ptr obj, ci_ptr key) {
	ci_ptr method = bb_proto_find(c->vm, obj, key);
	if (!method)
		bb_error("METHODBIND: method not found");
	if (!CI_IS_CLOSURE(method))
		bb_error("METHODBIND: method is not function type");

	bb_closure *bound = bb_vm_closure(c->vm, ((bb_closure *)method)->fn);
	ci_inc(obj);
	bound->self = obj;
	return (ci_ptr)bound;
}

static inline ci_ptr bb_op_arraccess(bb_coro *c, ci_ptr arr, ci_ptr idx) {
	(void)c;
	if (!CI_IS_ANY_ARR(arr))
		bb_error("ARRACCESS: operand is not an array");
	if (!CI_IS_INT(idx))
		bb_error("ARRACCESS: index must be integer");

	intptr_t index = CI_INT(idx);
	uint32_t alen = ci_arr_len((const ci_array *)arr);
	if (index < 0 || index >= (intptr_t)alen)
		return NULL;
	return ci_arr_index((const ci_array *)arr, (uint32_t)index);
}

/* ================================================================
 *  RRR special (no return value, not in table)
 * ================================================================ */

static inline void bb_op_arraystore(bb_vm *vm, ci_ptr arr, ci_ptr idx, ci_ptr val) {
	(void)vm;
	if (!CI_IS_ANY_ARR(arr))
		bb_error("ARRAYSTORE: operand is not an array");
	if (!CI_IS_INT(idx))
		bb_error("ARRAYSTORE: index must be integer");

	intptr_t index = CI_INT(idx);
	if (index < 0)
		bb_error("ARRAYSTORE: negative index %lld", (long long)index);

	ci_array *a = (ci_array *)arr;
	uint32_t alen = ci_arr_len(a);
	if ((uint32_t)index >= alen)
		ci_arr_extend(a, (uint32_t)index + 1);

	ci_inc(val);
	ci_ptr old = ci_arr_index(a, (uint32_t)index);
	ci_dec(old);
	ci_arr_set(a, (uint32_t)index, val);
}

static inline void bb_op_hashstore(bb_vm *vm, ci_ptr map, ci_ptr key, ci_ptr val) {
	(void)vm;
	if (!CI_IS_MAP(map))
		bb_error("HASHSTORE: operand is not a map");
	ci_inc(val);
	ci_map_put((ci_map *)map, key, val);
}

/* ================================================================
 *  RI16 ops  —  void fn(coro, &stack[b1], imm16)
 * ================================================================ */

static inline void bb_op_loadint_ri16(bb_coro *c, ci_ptr *slot, uint16_t imm) {
	(void)c;
	ci_dec(*slot);
	*slot = CI_PACKINT((int16_t)imm);
}

static inline void bb_op_loadstr_ri16(bb_coro *c, ci_ptr *slot, uint16_t imm) {
	bb_function *fn = c->fstack[c->fstack_pos - 1].function;
	if (imm >= fn->unit->str_count)
		bb_error("LOADSTR: string index %u out of range", imm);
	ci_dec(*slot);
	*slot = fn->unit->str2intern[imm];
}

static inline void bb_op_jmp_ri16(bb_coro *c, ci_ptr *slot, uint16_t imm) {
	(void)slot;
	c->pc = imm;
}

static inline void bb_op_jmpf_ri16(bb_coro *c, ci_ptr *slot, uint16_t imm) {
	if (CI_IS_FALSY(*slot))
		c->pc = imm;
}

static inline void bb_op_jmpt_ri16(bb_coro *c, ci_ptr *slot, uint16_t imm) {
	if (!CI_IS_FALSY(*slot))
		c->pc = imm;
}

static inline void bb_op_loadfn_ri16(bb_coro *c, ci_ptr *slot, uint16_t imm) {
	bb_function *fn = c->fstack[c->fstack_pos - 1].function;
	bb_function *f = ci_arr_index(fn->unit->functions, imm);
	bb_closure *cl = bb_vm_closure(c->vm, f);
	ci_dec(*slot);
	*slot = (ci_ptr)cl;
}

/* ================================================================
 *  VAR ops  —  void fn(coro, stack, &code[off])
 *  Convention: op[0]=opbyte, op[1]=b1, op[2]=extra_words, op[3]=b3
 *  Payload starts at op+4. Caller does pc += op[2].
 * ================================================================ */

static inline void bb_op_newmap_var(bb_coro *c, ci_ptr *stack, uint8_t *op) {
	bb_function *fn = c->fstack[c->fstack_pos - 1].function;
	uint8_t dst_reg    = op[1];
	uint8_t pair_count = op[3];

	ci_map *new_map = ci_map_new(16);

	if (pair_count > 0) {
		/* VAR_STRID: payload is [val_reg][u16 strid]... */
		uint8_t *pay = op + 4;
		for (uint32_t i = 0; i < pair_count; i++) {
			uint8_t val_reg = pay[i * 3];
			uint16_t strid = (uint16_t)pay[i * 3 + 1]
			               | ((uint16_t)pay[i * 3 + 2] << 8);
			if (strid >= fn->unit->str_count)
				bb_error("NEWMAP: string index %u out of range", strid);
			ci_ptr val = stack[val_reg];
			ci_ptr key = fn->unit->str2intern[strid];
			ci_inc(val);
			ci_map_put(new_map, key, val);
		}
	} else {
		/* VAR: payload is [pad 4B][type][val_reg][key]... */
		uint8_t *pay = op + 8;
		uint32_t budget = op[2] * 4;
		for (uint32_t i = 0; i < budget; ) {
			uint8_t type = pay[i];
			if (type > 1)
				break;

			uint8_t val_reg = pay[i + 1];
			ci_ptr val = stack[val_reg];
			ci_ptr key;

			if (type == 0) {
				uint16_t strid = (uint16_t)pay[i + 2]
				               | ((uint16_t)pay[i + 3] << 8);
				if (strid >= fn->unit->str_count)
					bb_error("NEWMAP: string index %u out of range", strid);
				key = fn->unit->str2intern[strid];
				i += 4;
			} else {
				key = stack[pay[i + 2]];
				i += 3;
			}

			ci_inc(val);
			ci_map_put(new_map, key, val);
		}
	}

	ci_inc((ci_ptr)new_map);
	ci_dec(stack[dst_reg]);
	stack[dst_reg] = (ci_ptr)new_map;
}

static inline void bb_op_newarray_var(bb_coro *c, ci_ptr *stack, uint8_t *op) {
	(void)c;
	uint8_t dst_reg    = op[1];
	uint8_t elem_count = op[3];

	ci_array *new_array = ci_arr_new(elem_count > 0 ? elem_count : 4);

	uint8_t *pay = op + 4;
	for (uint32_t i = 0; i < elem_count; i++) {
		ci_ptr elem = stack[pay[i]];
		ci_inc(elem);
		ci_arr_push(new_array, elem);
	}

	ci_inc((ci_ptr)new_array);
	ci_dec(stack[dst_reg]);
	stack[dst_reg] = (ci_ptr)new_array;
}

static inline void bb_op_hashaccess_var(bb_coro *c, ci_ptr *stack, uint8_t *op) {
	bb_function *fn = c->fstack[c->fstack_pos - 1].function;
	uint8_t dst_reg      = op[1];
	uint8_t string_count = op[3];

	uint8_t *pay = op + 4;
	uint8_t src_reg = pay[0];
	ci_ptr current = stack[src_reg];

	for (uint32_t i = 0; i < string_count; i++) {
		if (!current)
			break;
		uint16_t strid = (uint16_t)pay[1 + i * 2]
		               | ((uint16_t)pay[1 + i * 2 + 1] << 8);
		if (strid >= fn->unit->str_count)
			bb_error("HASHACCESS: string index %u out of range", strid);
		current = bb_proto_find(c->vm, current, fn->unit->str2intern[strid]);
	}

	ci_inc(current);
	ci_dec(stack[dst_reg]);
	stack[dst_reg] = current;
}

/* ================================================================
 *  Reglist VAR ops  —  LOADNULL / RETURN
 *  Wire: [11|op][count][extra][r0] + [r1..rN-1] padded
 *  count=0: no regs.  count=1: r0 in header, no payload.
 *  count>1: r0 in header, r1..rN-1 in payload.
 * ================================================================ */

static inline void bb_op_loadnull_var(bb_coro *c, ci_ptr *stack, uint8_t *op) {
	(void)c;
	uint8_t count = op[1];
	if (count == 0) return;
	uint8_t r0 = op[3];
	ci_dec(stack[r0]);
	stack[r0] = NULL;
	uint8_t *pay = op + 4;
	for (uint32_t i = 0; i < (uint32_t)(count - 1); i++) {
		uint8_t reg = pay[i];
		ci_dec(stack[reg]);
		stack[reg] = NULL;
	}
}

static inline void bb_op_return_var(bb_coro *c, ci_ptr *stack, uint8_t *op) {
	uint8_t count = op[1];

	/* gather return register indices */
	uint8_t src_regs[256];
	if (count >= 1) src_regs[0] = op[3];
	uint8_t *pay = op + 4;
	for (uint32_t i = 1; i < count; i++)
		src_regs[i] = pay[i - 1];

	/* snapshot return values (inc before any frame teardown) */
	ci_ptr ret_vals[256];
	for (uint32_t i = 0; i < count; i++) {
		ret_vals[i] = stack[src_regs[i]];
		ci_inc(ret_vals[i]);
	}

	if (c->fstack_pos == 1) {
		/* top-level return — store results and mark done */
		bb_frame *frame = &c->fstack[0];
		uint32_t ret_base = frame->stack_base + frame->function->regs;
		if (!ci_arr_ensure_space(c->stack, count))
			bb_vm_error(c->vm, "return: oom");
		for (uint32_t i = 0; i < count; i++)
			c->stack->data[ret_base + i] = ret_vals[i];
		c->stack->length  = ret_base + count;
		c->lastreturn_idx = ret_base;
		c->lastreturn_cnt = count;
		c->flags = BB_CORO_DONE;
		return;
	}

	/* pop callee frame, restore caller */
	c->fstack_pos--;
	bb_frame *callee = &c->fstack[c->fstack_pos];   /* just-popped */
	bb_frame *caller = &c->fstack[c->fstack_pos - 1];
	uint32_t caller_base = caller->stack_base;
	ci_ptr *caller_stack = c->stack->data + caller_base;

	/* copy return values into caller's ret_regs */
	uint32_t nrets = callee->ret_count;
	for (uint32_t i = 0; i < nrets && i < count; i++) {
		VM_DBG("[RET] ret_vals[%u] -> R(%u)\n", i, callee->ret_regs[i]);
		ci_dec(caller_stack[callee->ret_regs[i]]);
		caller_stack[callee->ret_regs[i]] = ret_vals[i];
	}
	/* release any extra ret_vals not consumed */
	for (uint32_t i = nrets; i < count; i++)
		ci_dec(ret_vals[i]);

	c->stack->length = caller_base + 256;
	c->pc = callee->ret_pc;
	c->flags = BB_CORO_RETURNED;
}

static inline void bb_op_call_var(bb_coro *c, ci_ptr *stack, uint8_t *op) {
	bb_vm *vm = c->vm;
	uint8_t fn_reg   = op[1];
	uint8_t extra    = op[2];
	uint8_t self_reg = op[3];

	uint8_t *pay  = op + 4;
	uint8_t nargs = pay[0];
	uint8_t nrets = pay[1];

	uint8_t arg_regs[256];
	uint8_t ret_regs[4];
	for (uint32_t i = 0; i < nargs; i++)
		arg_regs[i] = pay[2 + i];
	for (uint32_t i = 0; i < nrets && i < 4; i++)
		ret_regs[i] = pay[2 + nargs + i];

	ci_ptr fn_val = stack[fn_reg];
	if (!fn_val)
		bb_error("CALL: nil callee");
	bb_closure *cl = (bb_closure *)fn_val;

	if (cl->fn->name && CI_IS_ANY_STR(cl->fn->name))
		VM_DBG("[CALL] FN '%.*s'\n",
		       (int)ci_str_len(cl->fn->name),
		       (char *)ci_str_head(cl->fn->name));
	else
		VM_DBG("[CALL] FN <unnamed>\n");

	/* native call — no frame push, handle inline */
	if (cl->fn->flags & BB_FN_NATIVE) {
		ci_ptr result;
		ci_ptr self_val = cl->self ? cl->self : stack[self_reg];

		if (cl->fn->flags & BB_FN_NATIVE_VAR) {
			ci_ptr gathered[256];
			for (uint32_t i = 0; i < nargs; i++)
				gathered[i] = stack[arg_regs[i]];
			result = cl->fn->cfn_var(vm, nargs, gathered);
		} else if (cl->fn->flags & BB_FN_NATIVE_METHOD) {
			result = cl->fn->cfn(vm,
				self_val,
				nargs > 0 ? stack[arg_regs[0]] : NULL,
				nargs > 1 ? stack[arg_regs[1]] : NULL);
		} else {
			result = cl->fn->cfn(vm,
				nargs > 0 ? stack[arg_regs[0]] : NULL,
				nargs > 1 ? stack[arg_regs[1]] : NULL,
				nargs > 2 ? stack[arg_regs[2]] : NULL);
		}

		if (nrets > 0 && result) {
			ci_inc(result);
			ci_dec(stack[ret_regs[0]]);
			stack[ret_regs[0]] = result;
		}
		return;  /* no frame change, dispatch continues normally */
	}

	/* bytecode call — push frame, loop will reenter */
	uint32_t ret_pc = c->pc + extra;
	bb_coro_pushcall(c, cl->fn);

	/* save return info in callee frame */
	bb_frame *callee = &c->fstack[c->fstack_pos - 1];
	callee->ret_pc = ret_pc;
	callee->ret_count = nrets < 4 ? nrets : 4;
	for (uint32_t i = 0; i < callee->ret_count; i++)
		callee->ret_regs[i] = ret_regs[i];

	/* refresh stack pointer (pushcall may have grown the array) */
	ci_ptr *caller_stack = c->stack->data + (callee - 1)->stack_base;
	uint32_t callee_base = callee->stack_base;
	ci_ptr *callee_stack = c->stack->data + callee_base;

	/* set up self */
	ci_ptr self_val = cl->self ? cl->self : caller_stack[self_reg];
	ci_inc(self_val);
	callee_stack[0] = self_val;

	/* copy args */
	for (uint32_t i = 0; i < nargs; i++) {
		ci_ptr v = caller_stack[arg_regs[i]];
		ci_inc(v);
		callee_stack[1 + i] = v;
	}

	c->pc = 0;
	c->flags = BB_CORO_RETURNED;  /* signal reenter */
}

