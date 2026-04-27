
/* ================================================================
 *  Handler types
 * ================================================================ */

/*
 * VAR handler convention:
 *   c    — coroutine
 *   a    — r1 from header (meaning is op-specific, see below)
 *   b    — r2 from header (meaning is op-specific)
 *   list — pointer to payload (byte 8 of instruction, right after the 8-byte header)
 *
 *   To reach header fields from list:
 *     list[-5] = r3 = nwords (payload word count)
 *     list[-4] = imm[0]  (fn_reg for CALL, 0 otherwise)
 */
typedef void (*bb_var_fn)(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list);

/* ================================================================
 *  RRR ops  —  ci_ptr fn(coro, a, b)
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
	if (!CI_IS_MAP(map))
		bb_coro_error(c, "MAPACCESS: operand is not a MAP");
	return ci_map_find(map, key);
}

static inline ci_ptr bb_op_methodbind(bb_coro *c, ci_ptr obj, ci_ptr key) {
	ci_ptr method = bb_proto_find(c->vm, obj, key);
	if (!method)
		bb_coro_error(c, "METHODBIND: method not found");
	if (!CI_IS_CLOSURE(method))
		bb_coro_error(c, "METHODBIND: method is not function type");

	bb_closure *bound = bb_vm_closure(c->vm, ((bb_closure *)method)->fn);
	ci_inc(obj);
	bound->self = obj;
	return (ci_ptr)bound;
}

static inline ci_ptr bb_op_arraccess(bb_coro *c, ci_ptr arr, ci_ptr idx) {
	if (!CI_IS_ANY_ARR(arr))
		bb_coro_error(c, "ARRACCESS: operand is not an array");
	if (!CI_IS_INT(idx))
		bb_coro_error(c, "ARRACCESS: index must be integer");

	intptr_t index = CI_INT(idx);
	uint32_t alen = ci_arr_len((const ci_array *)arr);
	if (index < 0 || index >= (intptr_t)alen)
		return NULL;
	return ci_arr_index((const ci_array *)arr, (uint32_t)index);
}

/* ================================================================
 *  RRR special (no return value, not in table)
 * ================================================================ */

static inline void bb_op_arraystore(bb_coro *c, ci_ptr arr, ci_ptr idx, ci_ptr val) {
	if (!CI_IS_ANY_ARR(arr))
		bb_coro_error(c, "ARRAYSTORE: operand is not an array");
	if (!CI_IS_INT(idx))
		bb_coro_error(c, "ARRAYSTORE: index must be integer");

	intptr_t index = CI_INT(idx);
	if (index < 0)
		bb_coro_error(c, "ARRAYSTORE: negative index %lld", (long long)index);

	ci_array *a = (ci_array *)arr;
	uint32_t alen = ci_arr_len(a);
	if ((uint32_t)index >= alen)
		ci_arr_extend(a, (uint32_t)index + 1);

	ci_inc(val);
	ci_ptr old = ci_arr_index(a, (uint32_t)index);
	ci_dec(old);
	ci_arr_set(a, (uint32_t)index, val);
}

static inline void bb_op_hashstore(bb_coro *c, ci_ptr map, ci_ptr key, ci_ptr val) {
	if (!CI_IS_MAP(map))
		bb_coro_error(c, "HASHSTORE: operand is not a map");
	ci_inc(val);
	ci_map_put((ci_map *)map, key, val);
}

/* ================================================================
 *  VAR ops
 *
 *  NEWMAP:      a=dst,   b=npairs,   list=[type,val,klo,khi]*npairs
 *  NEWARRAY:    a=dst,   b=nelem,    list=[e0,e1,...eN-1]
 *  HASHACCESS:  a=dst,   b=src,      list=[strid_lo,strid_hi,...] (nwords*2 strids)
 *               list[-5] = r3 = nwords
 *  LOADNULL:    a=nregs, b=0,        list=[reg0,...,regN-1]
 *  RETURN:      a=nrets, b=0,        list=[reg0,...,regN-1]
 *  CALL:        a=nargs, b=nrets,    list[-4]=fn_reg,
 *               list=[self_reg, arg0..argN-1, ret0..retM-1]
 * ================================================================ */

/* NEWMAP: a=dst, b=npairs, list=[type,val,klo,khi]*npairs
 *   type 0: key = strid (u16 klo|khi)
 *   type 1: key = register (klo = reg, khi = 0) */
static void bb_op_newmap_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	ci_ptr *stack = c->fast_stack;
	bb_function *fn = bb_coro_frame_top(c)->function;
	uint32_t dst_reg = (uint32_t)a;
	uint32_t npairs  = (uint32_t)b;

	ci_map *new_map = ci_map_new(16);

	for (uint32_t i = 0; i < npairs; i++) {
		uint8_t type    = list[i * 4 + 0];
		uint8_t val_reg = list[i * 4 + 1];
		ci_ptr val = stack[val_reg];
		ci_ptr key;

		if (type == 0) {
			uint16_t strid = (uint16_t)list[i * 4 + 2] | ((uint16_t)list[i * 4 + 3] << 8);
			if (strid >= fn->unit->str_count)
				bb_coro_error(c, "NEWMAP: string index %u out of range", strid);
			key = fn->unit->str2intern[strid];
		} else {
			key = stack[list[i * 4 + 2]];
		}

		ci_inc(val);
		ci_map_put(new_map, key, val);
	}

	ci_inc((ci_ptr)new_map);
	ci_dec(stack[dst_reg]);
	stack[dst_reg] = (ci_ptr)new_map;
}

/* NEWARRAY: a=dst, b=nelem, list=[e0..eN-1] */
static void bb_op_newarray_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	ci_ptr *stack = c->fast_stack;
	uint32_t dst_reg    = (uint32_t)a;
	uint32_t elem_count = (uint32_t)b;

	ci_array *new_array = ci_arr_new(elem_count > 0 ? elem_count : 4);

	for (uint32_t i = 0; i < elem_count; i++) {
		ci_ptr elem = stack[list[i]];
		ci_inc(elem);
		ci_arr_push(new_array, elem);
	}

	ci_inc((ci_ptr)new_array);
	ci_dec(stack[dst_reg]);
	stack[dst_reg] = (ci_ptr)new_array;
}

/* HASHACCESS: a=dst, b=src, list=strids (2 per word); nwords = list[-5] */
static void bb_op_hashaccess_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	ci_ptr *stack = c->fast_stack;
	bb_function *fn = bb_coro_frame_top(c)->function;
	uint32_t dst_reg = (uint32_t)a;
	uint32_t src_reg = (uint32_t)b;
	uint32_t nkeys   = (uint32_t)list[-5];  /* r3 = nwords; used as key count */

	ci_ptr current = stack[src_reg];

	for (uint32_t i = 0; i < nkeys; i++) {
		if (!current) break;
		uint16_t strid = (uint16_t)list[i * 2] | ((uint16_t)list[i * 2 + 1] << 8);
		if (strid >= fn->unit->str_count)
			bb_coro_error(c, "HASHACCESS: string index %u out of range", strid);
		current = bb_proto_find(c->vm, current, fn->unit->str2intern[strid]);
	}

	ci_inc(current);
	ci_dec(stack[dst_reg]);
	stack[dst_reg] = current;
}

/* LOADNULL: a=nregs, b=0, list=[reg0..regN-1] */
static void bb_op_loadnull_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	(void)b;
	ci_ptr *stack = c->fast_stack;
	uint32_t count = (uint32_t)a;

	for (uint32_t i = 0; i < count; i++) {
		ci_dec(stack[list[i]]);
		stack[list[i]] = NULL;
	}
}

/* RETURN: a=nrets, b=0, list=[reg0..regN-1] */
static void bb_op_return_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	(void)b;
	ci_ptr *stack = c->fast_stack;
	uint32_t count = (uint32_t)a;
	
	if (c->fstack_pos == 1) {
		bb_frame *frame = bb_coro_frame(c, 0);
		uint32_t ret_base = frame->stack_base + frame->function->regs;
		if (!ci_arr_ensure_space(c->stack, count))
			bb_vm_error(c->vm, "return: oom");

		c->stack->length  = ret_base + count;
		c->lastreturn_idx = ret_base;
		c->lastreturn_cnt = count;
		c->flags = BB_CORO_DONE;
		return;
	}

	bb_frame *callee = bb_coro_frame_top(c);
	bb_frame *caller = bb_coro_frame_caller(c);
	
	bb_cached_op* call_opcode = caller->pc - 1;
	
	uint32_t nargs = call_opcode->a;
	uint32_t nrets = call_opcode->b;
	uint8_t* rets_list = ((uint8_t*)call_opcode->c) + 1 + nargs;
	
	ci_ptr *caller_stack = c->stack->data + caller->stack_base;

	for (uint32_t i = 0; i < nrets && i < count; i++) {
		VM_DBG("[RET] callee[%u] -> caller[%u]\n", list[i], rets_list[i]);
		
		ci_ptr* callee_reg = &stack[ list[i] ];
		
		ci_ptr* caller_reg = &caller_stack[ rets_list[i] ];
		
		// caller reg is decr and nulled by caller
		*caller_reg = *callee_reg;
		
		// this introduces bug when returnign same value return a, a
		// dont touch refcnt this value
		*callee_reg = NULL;
	}
	
	
	bb_coro_popcall(c);
}

/* CALL: a=nargs, b=nrets, list[-4]=fn_reg,
 *       list=[self_reg, arg0..argN-1, ret0..retM-1] */
static void bb_op_call_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	bb_vm *vm = c->vm;
	ci_ptr *stack  = c->fast_stack;
	uint8_t fn_reg   = list[-4];
	uint32_t nargs   = (uint32_t)a;
	uint32_t nrets   = (uint32_t)b;
	uint8_t self_reg = list[0];

	/* list: [self] [a0..aN-1] [r0..rM-1] */
	uint8_t *arg_list = list + 1;          
	uint8_t *ret_list = list + 1 + nargs;  

	ci_ptr fn_val = stack[fn_reg];
	if (!fn_val)
		bb_coro_error(c, "CALL: nil callee");
	bb_closure *cl = (bb_closure *)fn_val;

	if (cl->fn->name && CI_IS_ANY_STR(cl->fn->name))
		VM_DBG("[CALL] FN '%.*s' nargs %d nrets %d, fn_reg %d, self %d\n",
		       (int)ci_str_len(cl->fn->name),
		       (char *)ci_str_head(cl->fn->name),
		        nargs, nrets, fn_reg, self_reg
		);
	else
		VM_DBG("[CALL] FN <unnamed>\n");

	//zero out all rets
	ci_ptr *dst = NULL;
	
	while(nrets){
		nrets--;
		
		dst = &stack[ ret_list[nrets] ];
		ci_dec(*dst);
		
		*dst = NULL;
	} 
	
	
	if (cl->fn->flags & BB_FN_NATIVE) {
		ci_ptr result;
		ci_ptr self_val = cl->self ? cl->self : stack[self_reg];

		/* Allow native functions to access the current coroutine */
		bb_coro *saved_coro = vm->current_coro;
		vm->current_coro = c;
		
		if (cl->fn->flags & BB_FN_NATIVE_METHOD) {
			if (cl->fn->flags & BB_FN_NATIVE_VAR) {
				ci_ptr gathered[256];
				gathered[0] = self_val;
				for (uint32_t i = 0; i < nargs; i++)
					gathered[i + 1] = stack[arg_list[i]];
				result = cl->fn->cfn_var(vm, (uint8_t)(nargs + 1), gathered);
			} else {
				result = cl->fn->cfn(vm, self_val,
					nargs > 0 ? stack[arg_list[0]] : NULL,
					nargs > 1 ? stack[arg_list[1]] : NULL);
			}
		} else if (cl->fn->flags & BB_FN_NATIVE_VAR) {
			ci_ptr gathered[256];
			for (uint32_t i = 0; i < nargs; i++)
				gathered[i] = stack[arg_list[i]];
			result = cl->fn->cfn_var(vm, (uint8_t)nargs, gathered);
		} else {
			result = cl->fn->cfn(vm,
				nargs > 0 ? stack[arg_list[0]] : NULL,
				nargs > 1 ? stack[arg_list[1]] : NULL,
				nargs > 2 ? stack[arg_list[2]] : NULL);
		}

		vm->current_coro = saved_coro;

		if (dst) {
			//ci_inc(result); c call should return value with refcnt 1 or more
			*dst = result;
		} else {
			ci_dec(result);
		}

		return;
	}

	/* bytecode call — push frame */
	bb_coro_pushcall(c, cl->fn);
	
	VM_DBG("[CALL] exec start\n");
	
	return;
}
