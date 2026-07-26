
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

VM_OP_INLINE void bb_op_hashstore(bb_coro *c, ci_ptr map, ci_ptr key, ci_ptr val);

/* ================================================================
 *  RRR ops  —  ci_ptr fn(coro, a, b)
 * ================================================================ */

static inline ci_ptr bb_op_move(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)b;
	
	ci_inc(a);
	return a;
}

VM_OP_INLINE ci_ptr bb_op_hashaccess_rrr(bb_coro *c, ci_ptr a, ci_ptr b) {
	ci_map* m = a;
	ci_ptr key = b;
	
	BB_RETURN(bb_proto_find(c->vm, a, b));
	
	if (!CI_IS_MAP(a))
		BB_RETURN(bb_proto_find(c->vm, a, b));
	
	do {
		ci_map_kv* kv = ci_map_find_kv_inline(m, key);
		if(kv) BB_RETURN(kv->val);
	} while ( (m = ((const ci_map *)m)->prototype) );
		
	return NULL;
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
	BB_RETURN(ci_map_find(map, key));
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
	BB_RETURN_NOINC(bound);
}

static inline ci_ptr bb_op_arraccess(bb_coro *c, ci_ptr arr, ci_ptr idx) {
	if (!CI_IS_ANY_ARR(arr)) {
		if (CI_IS_MAP(arr))
			BB_RETURN(bb_proto_find(c->vm, arr, idx));

		BB_META_DISPATCH_INDEX_GET(c, arr, idx, "ARRACCESS: operand is not an array");
	}

	if (!CI_IS_INT(idx))
		bb_coro_error(c, "ARRACCESS: index must be integer");

	const ci_array *a = (const ci_array *)arr;
	uint32_t i = ci_arr_wrapindex(a, CI_INT(idx));

	if (i >= ci_arr_len(a))
		return NULL;

	BB_RETURN(ci_arr_index(a, i));
}

/* ================================================================
 *  RRR special (no return value, not in table)
 * ================================================================ */

static inline void bb_op_arraystore(bb_coro *c, ci_ptr arr, ci_ptr idx, ci_ptr val) {
	if (!CI_IS_ANY_ARR(arr)) {
		if (CI_IS_MAP(arr))
			return bb_op_hashstore(c, arr, idx, val);

		BB_META_DISPATCH_INDEX_SET(c, arr, idx, val, "ARRAYSTORE: operand is not an array");
	}

	if (!CI_IS_INT(idx))
		bb_coro_error(c, "ARRAYSTORE: index must be integer");

	ci_array *a = (ci_array *)arr;
	uint32_t i = ci_arr_wrapindex(a, CI_INT(idx));

	if (i >= ci_arr_len(a))
		ci_arr_extend(a, i + 1);

	ci_inc(val);
	ci_ptr old = ci_arr_index(a, i);
	ci_dec(old);
	ci_arr_set(a, i, val);
}

/* ================================================================
 *  Iterator ops
 * ================================================================ */

/* ITERINIT: a=iterable_reg, b=iterator_reg, c=cursor_reg
 * Copies iterable into iterator reg, sets cursor to 0.
 * For ordered maps: creates a ci_tree_iter GC object as the iterator. */
VM_OP static void __vmop_iterinit(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	VM_OP_ACCESS_STACK;
	ci_ptr iterable = VM_OP_STACK(a);

	if (CI_IS_ORDERED_MAP(iterable)) {
		ci_tree_iter *it = ci_tree_iter_new((ci_tree *)iterable);
		VM_OP_SET_STACK(b, (ci_ptr)it);
		VM_OP_SET_STACK(_c, CI_PACKINT(0));
	}
	else {
		ci_inc(iterable);
		VM_OP_SET_STACK(b, iterable);
		VM_OP_SET_STACK(_c, CI_PACKINT(0));
	}
	BB_DISPATCH_NEXT(c);
}

/* ITERSTEP: a=first_reg, b=nregs, c=jump_target
 * regs layout: [iterator, cursor, var0, var1, ...]
 * Advances cursor, fills vars, jumps to c when done */
VM_OP static void __vmop_iterstep(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	VM_OP_ACCESS_STACK;
	ci_ptr *regs = &sk[a];
	uint32_t nregs = (uint32_t)b;
	ci_ptr iterator = regs[0];
	ci_ptr cursor   = regs[1];
	intptr_t idx = cursor ? CI_INT(cursor) : 0;

	if (CI_IS_ANY_ARR(iterator)) {
		ci_array *arr = (ci_array *)iterator;
		uint32_t len = ci_arr_len(arr);

		if (idx >= (intptr_t)len) goto end_looping;

		/* var0 = index, var1 = value (if requested) */
		if (nregs >= 1) {
			ci_dec(regs[2]);
			regs[2] = CI_PACKINT(idx);
		}
		if (nregs >= 2) {
			ci_ptr val = ci_arr_index(arr, (uint32_t)idx);
			ci_inc(val);
			ci_dec(regs[3]);
			regs[3] = val;
		}

		regs[1] = CI_PACKINT(idx + 1);
	}
	else if (CI_IS_MAP(iterator)) {
		ci_map *map = (ci_map *)iterator;
		uint32_t cursor = (uint32_t)idx;
		ci_map_kv *kv = ci_map_next(map, &cursor);

		if (!kv) goto end_looping;

		if (nregs >= 1) {
			ci_inc(kv->key);
			ci_dec(regs[2]);
			regs[2] = kv->key;
		}
		if (nregs >= 2) {
			ci_inc(kv->val);
			ci_dec(regs[3]);
			regs[3] = kv->val;
		}

		regs[1] = CI_PACKINT(cursor);
	}
	else if (CI_IS_TREE_ITER(iterator)) {
		ci_tree_iter *it = (ci_tree_iter *)iterator;
		ci_map_kv *kv = ci_tree_iter_next(it);

		if (!kv) goto end_looping;

		if (nregs >= 1) {
			ci_inc(kv->key);
			ci_dec(regs[2]);
			regs[2] = kv->key;
		}
		if (nregs >= 2) {
			ci_inc(kv->val);
			ci_dec(regs[3]);
			regs[3] = kv->val;
		}
	}
	else {
		bb_coro_error(c, "ITERSTEP: not iterable");
	}

	
	BB_DISPATCH_NEXT(c);
	
	end_looping: {
		c->pc = c->ops_base + _c;
		BB_DISPATCH_NEXT(c);
	}
}

VM_OP_INLINE void bb_op_hashstore(bb_coro *c, ci_ptr map, ci_ptr key, ci_ptr val) {
	if (!CI_IS_MAP(map))
		bb_coro_error(c, "HASHSTORE: operand is not a map");
	ci_inc(key);
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
 *  CALL:        a=base, b=nargs, c=nrets (RRR dispatch)
 *  MOVETO:      a=base, b=cnt, list=[src_regs]
 *  MOVEFROM:    a=base, b=cnt, list=[dst_regs]
 * ================================================================ */

/* MOVETO: a=base, b=cnt, list=[src0..srcN-1]
 * copies list[i] -> stack[base+i], skips src==dst */
static void bb_op_moveto_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	ci_ptr *stack = c->fast_stack;
	uint32_t base = (uint32_t)a;
	uint32_t cnt  = (uint32_t)b;

	while (cnt--) {
		uint8_t src = *list++;
		if (src != base) {
			ci_ptr val = stack[src];
			ci_inc(val);
			ci_dec(stack[base]);
			stack[base] = val;
		}
		base++;
	}
}

/* MOVEFROM: a=base, b=cnt, list=[dst0..dstN-1]
 * copies stack[base+i] -> list[i], skips src==dst */
static void bb_op_movefrom_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	ci_ptr *stack = c->fast_stack;
	uint32_t base = (uint32_t)a;
	uint32_t cnt  = (uint32_t)b;

	while (cnt--) {
		uint8_t dst = *list++;
		if (dst != base) {
			ci_ptr val = stack[base];
			ci_inc(val);
			ci_dec(stack[dst]);
			stack[dst] = val;
		}
		base++;
	}
}

/* NEWMAP: a=dst, b=npairs, list=[type,val,klo,khi]*npairs
 *   type 0: key = strid (u16 klo|khi)
 *   type 1: key = register (klo = reg, khi = 0) */
static void bb_op_newmap_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	ci_ptr *stack = c->fast_stack;
	bb_function *fn = bb_coro_frame_function(bb_coro_frame_top(c));
	uint32_t dst_reg = (uint32_t)a;
	uint32_t npairs  = (uint32_t)b;

	ci_map *new_map = ci_map_new(16);
	printf("alloc map %p ref:%d\n", new_map, new_map->gc.refcnt);
	
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

	ci_dec(stack[dst_reg]);
	stack[dst_reg] = (ci_ptr)new_array;
}

/* HASHACCESS: a=dst, b=src, list=strids (2 per word); */
static inline void bb_op_hashaccess_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, uint8_t *list) {
	ci_ptr *stack = c->fast_stack;
	bb_function *fn = bb_coro_frame_function(bb_coro_frame_top(c));
	vm_dipatch_arg dst_reg = a;

	uint16_t *strings = (uint16_t *)list;

	ci_ptr current = stack[b];
	
	ci_ptr* strtable = fn->unit->str2intern;
	
	while(1) {
		ci_ptr key = strtable[*strings];
		ci_ptr map = current;
		
		//if (*strings >= fn->unit->str_count)
			//bb_coro_error(c, "HASHACCESS: string index %u out of range", *strings);
		
		if(CI_IS_MAP(current)){
			ci_map_kv* kv = ci_map_find_kv(map, key);
			
			if(kv){ 
				current = kv->val;
				goto lookup_next;
			}
		} else {
			goto full_lookup;
		}
		
		full_lookup:
			current = bb_proto_find(c->vm, map, key);
			if (!current) break;
			
		
		lookup_next:
		strings++;
		if (!*strings) break;
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
VM_OP static void bb_op_return_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	uint8_t *list = (uint8_t*) _c;
	
	ci_ptr *stack = c->fast_stack;
	uint32_t count = (uint32_t)a;
	
	bb_frame *caller = bb_coro_frame_caller(c);
	
	ci_ptr *caller_stack = c->stack->data + caller->stack_base;

	if(!caller->closure){
		ci_ptr *rets = caller_stack;
		rets[0] = CI_PACKINT(count);
		rets++;
		
		while (count--) {
			*rets = stack[*list];

			// dont touch refcnt this value
			stack[*list] = NULL;
			rets++;
			list++;
		}
		
		c->flags = BB_CORO_DONE;
		
		bb_frame *this_function = bb_coro_frame_top(c);
		ci_dec_multi(stack, this_function->closure->fn->regs);
		
		bb_coro_popcall(c);
		
		return;
	}
	
	bb_cached_op* call_opcode = caller->pc - 1;

	uint32_t call_base  = call_opcode->a;
	uint32_t call_nargs = call_opcode->b;
	uint32_t call_nrets = call_opcode->c;

	ci_ptr *rets = &caller_stack[call_base + 2 + call_nargs];

	uint32_t copy = call_nrets < count ? call_nrets : count;
	while (copy--) {
		// caller reg is decr and nulled by caller
		*rets = stack[*list];

		// dont touch refcnt this value
		stack[*list] = NULL;
		rets++;
		list++;
	}
	

	bb_frame *this_function = bb_coro_frame_top(c);
	ci_dec_multi(stack, this_function->closure->fn->regs);
	
	bb_coro_popcall(c);
	
	BB_DISPATCH_NEXT(c);
}

/* CALL: a=base, b=nargs, c=nrets
 * window layout: [fn][self][arg0..argN-1][ret0..retM-1] */
VM_OP static void __vmop_call(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	uint32_t base    = (uint32_t)a;
	uint32_t nargs   = (uint32_t)b;
	uint32_t nrets   = (uint32_t)_c;

	ci_ptr *win = &c->fast_stack[base];
	ci_ptr fn_val   = win[0];
	ci_ptr self_raw = win[1];
	
	if (!fn_val)
		bb_coro_error(c, "CALL: nil callee");
	
	if (!bb_is_closure(fn_val))
		bb_coro_error(c, "CALL: callee not closure");
	
	bb_closure *cl = (bb_closure *)fn_val;

	VM_DBG("[CALL] base=%u nargs=%u nrets=%u\n", base, nargs, nrets);

	ci_ptr self_val = cl->self ? cl->self : self_raw;

	if (cl->fn->flags & BB_FN_NATIVE) {
		ci_ptr *args    = win + 2;
		ci_ptr *rets    = win + 2 + nargs;
		
		ci_ptr result;

		if (cl->fn->flags & BB_FN_NATIVE_VAR) {
			size_t end = cl->fn->cfn_var(c, self_val, nargs, args, nrets);
			size_t written = end - nargs;
			nrets -= written;
			rets += written;
			goto zero_rets;

		} else if (cl->fn->flags & BB_FN_ADVANCED) {
			bb_fast_fn adv = (bb_fast_fn) (void*)cl->fn->cfn;
			
			printf("adv called with %p %p %p %p args\n", c, win, nargs, nrets);
			BB_MUSTTAIL return adv(c, (vm_dipatch_arg)win, nargs, nrets);
		} else {
			result = cl->fn->cfn(c, self_val,
				nargs > 0 ? args[0] : NULL,
				nargs > 1 ? args[1] : NULL,
				nargs > 2 ? args[2] : NULL);
		}

		
		if (nrets) {
			ci_dec(*rets);
			*rets = result;
			nrets--;
			rets++;
		} else {
			ci_dec(result);
		}

		zero_rets:
		while (nrets--) {
			ci_dec(*rets);
			*rets = NULL;
			rets++;
		}
		
		BB_DISPATCH_NEXT(c);
	}

	/* bytecode call — push frame */
	bb_coro_pushcall(c, cl);
	// this can realloc stack
	
	
	bb_frame *callee = bb_coro_frame_top(c);
	ci_ptr *callee_stack = c->stack->data + callee->stack_base;

	bb_frame *caller = bb_coro_frame_caller(c);
	ci_ptr *caller_stack = c->stack->data + caller->stack_base;
	
	ci_ptr *args    = (&caller_stack[base]) + 2;
	ci_ptr *rets    = args + nargs;
	
	ci_inc(self_val);
	callee_stack[0] = self_val;
	callee_stack++;

	uint32_t copy = nargs < cl->fn->args ? nargs : cl->fn->args;
	while (copy--) {
		*callee_stack = *args;
		ci_inc(*callee_stack);
		callee_stack++;
		args++;
	}

	
	while (nrets--) {
		ci_dec(*rets);
		*rets = NULL;
		rets++;
	}

	BB_DISPATCH_NEXT(c);
}
