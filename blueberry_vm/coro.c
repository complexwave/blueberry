/* ================================================================
 *  Coroutine
 * ================================================================ */

/* ================================================================
 *  Coroutine lifecycle
 * ================================================================ */

static void bb_coro_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	bb_coro *c = ptr;
	free(c->fstack);
	ci_dec(c->stack);
}

bb_frame* bb_coro_pushframe(bb_coro *c, uint32_t regs);

static bb_coro *bb_coro_new(bb_vm *vm) {
	bb_coro *c = ci_new(CI_BB_CORO);
	if (!c)
		bb_vm_error(vm, "bb_coro_new: out of memory");
	c->vm    = vm;
	c->flags = BB_CORO_NEW;
	c->pc    = 0;

	c->stack = ci_arr_new(256*3);

	c->fstack_cap = 32;
	c->fstack_pos = 0;
	c->fstack     = b_malloc(c->fstack_cap * sizeof(bb_frame));

	// stores coro return result and acts as end indicator
	bb_frame* frame = bb_coro_pushframe(c, 256);
	frame->closure = NULL;
	
	return c;
}

/* ================================================================
 *  Error handling
 * ================================================================ */

static void bb_coro_error(bb_coro *c, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	fflush(stderr);

	if (c && c->fstack_pos > 0) {
		bb_coro_dump_stack(c, 0);
	}

	exit(1);
}



/* ================================================================
 *  Call frame stack
 * ================================================================ */

bb_frame* bb_coro_pushframe(bb_coro *c, uint32_t regs) {
	if (c->fstack_pos >= c->fstack_cap)
		bb_coro_error(c, "frame stack overflow");

	if(c->fstack_pos){
		bb_frame *current_frame = bb_coro_frame_top(c);
		current_frame->pc = c->pc;
	}

	uint32_t base = c->stack->length;

	if (!ci_arr_ensure_space(c->stack, regs))
		bb_vm_error(c->vm, "stack: out of memory");
	
	memset(c->stack->data + base, 0, regs * sizeof(ci_ptr));

	c->stack->length += regs;

	bb_frame *frame = bb_coro_frame(c, c->fstack_pos);

	frame->pc     = 0;
	frame->stack_base = base;
	
	c->fstack_pos++;
	
	return frame;
}


static void bb_coro_pushcall(bb_coro *c, bb_closure *cl) {
	if(c->fstack_pos){
		bb_frame *current_frame = bb_coro_frame_top(c);
		current_frame->pc = c->pc;
	}
	
	bb_frame* frame = bb_coro_pushframe(c, 256);
	frame->closure    = cl;
	
	ci_ptr *stack   = c->stack->data + frame->stack_base;
	VM_DBG("[PUSH(%u) FRAME(%p)] fstack %p -> %p [%u]. \n",
		   c->fstack_pos-1, cl->fn, c->fast_stack, stack, frame->stack_base);

	stack[255]    = (ci_ptr)c->vm->globals;
	c->fast_stack = stack;

	bb_cached_op *ops = bb_function_ops(cl->fn);

	c->ops_base = ops;
	c->pc   = ops;
}


static void bb_coro_popcall(bb_coro *c) {
	bb_frame *frame = bb_coro_frame_top(c);
	bb_frame *caller = bb_coro_frame_caller(c);

	c->stack->length = caller->stack_base + 256;

	ci_ptr *stack   = c->stack->data + caller->stack_base;

	VM_DBG("[POP(%u) FRAME(%p)] fstack %p -> %p[%u]. pop pc %p \n",
		   c->fstack_pos, bb_coro_frame_function(frame), c->fast_stack, stack, frame->stack_base, caller->pc);
	c->fstack_pos--;
	
	c->fast_stack = stack;

	if(!caller->closure){
		return;
	}
		
	bb_cached_op *ops = bb_function_ops(bb_coro_frame_function(caller));

	c->ops_base = ops;
	c->pc   = caller->pc;
}

/* ================================================================
 *  Call interface
 * ================================================================ */

static ci_ptr bb_coro_call(bb_coro *c, bb_closure *cl, ci_ptr a, ci_ptr b, ci_ptr c_arg) {
	c->vm->current_coro = c;

	ci_ptr self = bb_closure_self(cl);

	if (cl->fn->flags & BB_FN_NATIVE) {
		if (cl->fn->flags & BB_FN_NATIVE_VAR) {
			ci_ptr gathered[4] = { a, b, c_arg, NULL };
			uint32_t n = !!a + !!b + !!c_arg;
			cl->fn->cfn_var(c, self, n, gathered, 1);
			return gathered[n];
		}
		return cl->fn->cfn(c, self, a, b, c_arg);
	}

	/* bytecode call */

	bb_coro_pushcall(c, cl);
	ci_ptr *sk = c->fast_stack;
	sk[0] = self;
	sk[1] = a;
	sk[2] = b;
	sk[3] = c_arg;

	c->flags = BB_CORO_RUNNING;
	bb_vm_execute(c);

	ci_ptr result = NULL;

	if (c->fstack_pos > 1)
		bb_coro_popcall(c);
	else
		c->fstack_pos = 0;


	return result;
}

static void bb_coro_call_var(bb_coro *c, bb_closure *cl,
                             ci_ptr *args, uint32_t nargs,
                             ci_ptr *rets, uint32_t nrets)
{
	c->vm->current_coro = c;

	if (cl->fn->flags & BB_FN_NATIVE) {
		ci_ptr self = cl->self ? cl->self : (nargs > 0 ? args[0] : NULL);
		ci_ptr result;

		if (cl->fn->flags & BB_FN_NATIVE_VAR) {
			/* args and rets are separate buffers here, not in-place.
			 * Build a combined window: [args...][ret slots...] */
			ci_ptr combined[64] = {};
			uint32_t cn = nargs < 32 ? nargs : 32;
			for (uint32_t i = 0; i < cn; i++) combined[i] = args[i];
			uint32_t rn = nrets < 32 ? nrets : 32;

			size_t end = cl->fn->cfn_var(c, self, cn, combined, rn);
			uint32_t written = (uint32_t)(end - cn);
			for (uint32_t i = 0; i < written && i < nrets; i++)
				rets[i] = combined[cn + i];
			for (uint32_t i = written; i < nrets; i++)
				rets[i] = NULL;
			return;
		} else {
			result = cl->fn->cfn(c, self,
				nargs > 0 ? args[0] : NULL,
				nargs > 1 ? args[1] : NULL,
				nargs > 2 ? args[2] : NULL);
		}

		if (nrets > 0) rets[0] = result;
		for (uint32_t i = 1; i < nrets; i++) rets[i] = NULL;
		return;
	}

	/* bytecode call */
	bb_coro_pushcall(c, cl);
	ci_ptr *sk = c->fast_stack;
	sk[0] = bb_closure_self(cl);
	for (uint32_t i = 0; i < nargs && i < cl->fn->args; i++)
		sk[i + 1] = args[i];

	c->flags = BB_CORO_RUNNING;
	bb_vm_execute(c);

	if (c->fstack_pos > 1)
		bb_coro_popcall(c);
	else
		c->fstack_pos = 0;

}


static void bb_coro_yield(bb_coro *c, ci_ptr val) {
	(void)val;
	bb_coro_error(c, "yield: not implemented");
}

