/*
 * blueberry_vm/proto/coro.c — coroutine prototype and Lua-like API
 *
 * Script API (coro.* namespace and methods):
 *
 *   Global namespace:
 *     coro.new(fn)           create new coroutine from function
 *
 *   Methods on coroutine objects:
 *     c.resume(...)          resume with varargs, returns varargs
 *     c.yield(...)           yield from within coroutine
 *     c.status()             returns "running" | "suspended" | "done" | "error"
 */

/* ================================================================
 *  Coroutine methods — work directly on bb_coro objects
 * ================================================================ */

/* c.status() — get coroutine status */
static ci_ptr bb_coro_status_method(bb_coro_arg *c, ci_ptr self) {
	BB_CHECK_CORO(self);
	bb_coro *coro = (bb_coro *)self;

	if (coro->flags & BB_CORO_RUNNING)
		return BB_CSTR(c->vm, "running");
	if (coro->flags & BB_CORO_DONE)
		return BB_CSTR(c->vm, "done");
	if (coro->flags & BB_CORO_ERROR)
		return BB_CSTR(c->vm, "error");
	return BB_CSTR(c->vm, "suspended");
}

/* c.resume(...) — resume coroutine with varargs */
static ci_ptr bb_coro_resume_method(bb_coro *c, ci_ptr coro_ptr, size_t nargs, ci_ptr *args) {
	(void)args;
	BB_CHECK_CORO(coro_ptr);
	bb_coro *coro = (bb_coro *)coro_ptr;


	/* TODO:
	 *  - Extract bb_coro from self
	 *  - Check state is suspended
	 *  - Marshal nargs into coro stack slots 1..nargs
	 *  - Resume execution (call bb_vm_execute or internal resume point)
	 *  - Capture lastreturn_* values
	 *  - Return as varargs via c->lastreturn_*
	 */

	printf("coro:resume called with %zu args\n", nargs);
	
	bb_frame *current_frame = bb_coro_frame_top(coro);
	bb_closure *cl = current_frame->closure;

	ci_ptr *sk = coro->fast_stack;
	sk[0] = bb_closure_self(cl);
	sk++;
	
	uint32_t copy_nargs = (nargs < cl->fn->args) ? nargs : cl->fn->args;
	for (uint32_t i = 0; i < copy_nargs; i++)
		sk[i] = args[i];

	coro->flags = BB_CORO_RUNNING;
	
	bb_vm_execute(coro);
	printf("coro:resume returned\n");
	
	
	bb_frame *rets_frame = bb_coro_frame_top(coro);
	
	ci_ptr *rets = coro->stack->data + rets_frame->stack_base;
	int32_t rets_cnt = CI_INT(rets[0]);
	
	printf("coro:resume returned %d %p\n", rets_cnt, rets);

	return NULL;
}

/* c.yield(...) — yield from coroutine (global function, not method) */
static ci_ptr bb_coro_yield_fn(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args) {
	(void)self;
	(void)args;

	/* TODO:
	 *  - Use bb_vm.current_coro to get calling coro
	 *  - Save nargs into lastreturn_cnt, store args in lastreturn_idx
	 *  - Set state to suspended
	 *  - Return from bb_vm_execute (longjmp or unwind stack)
	 *  - Resume caller with returned values
	 */

	printf("coro.yield called with %zu args\n", nargs);
	return NULL;
}

/* ================================================================
 *  Namespace: coro.new()
 * ================================================================ */

/* coro.new(fn) — create new coroutine from function */
static ci_ptr bb_coro_new_fn(bb_coro_arg *c, ci_ptr self, ci_ptr fn_arg) {
	(void)self;
	BB_CHECK_CLOSURE(fn_arg);
	bb_closure* cl = (bb_closure*)fn_arg;
	
	if (cl->fn->flags & BB_FN_NATIVE) {
		bb_coro_error(c, "Creating coro based on C function not supported");
	}
	
	bb_coro *coro = bb_coro_new(c->vm);
	
	bb_coro_pushcall(coro, cl);
	
	return (ci_ptr)coro;
}

/* ================================================================
 *  Registration
 * ================================================================ */

static void bb_lib_coro_init(bb_vm *vm) {
	/* Register "coro" prototype on all bb_coro objects */
	ci_map *proto = bb_proto_register(vm, "coro");
	
	bb_set_arena_prototype(CI_BB_CORO, proto);

	/* Register methods on coro prototype */
	static const bb_cfunc coro_lib[] = {
		{ "status", bb_coro_status_method,   0 },
		{ "resume", bb_coro_resume_method,   BB_FN_NATIVE_VAR },
		{ "yield",  bb_coro_yield_fn,        BB_FN_NATIVE_VAR },
	};
	bb_func2map(vm, proto, coro_lib, sizeof(coro_lib) / sizeof(coro_lib[0]));

	/* Create global coro namespace */
	ci_map *coro_ns = bb_proto_new_map(vm, "coro", 8);
	static const bb_cfunc coro_ns_lib[] = {
		{ "new", bb_coro_new_fn, 0 },
	};
	bb_func2map(vm, coro_ns, coro_ns_lib, sizeof(coro_ns_lib) / sizeof(coro_ns_lib[0]));

	ci_ptr coro_key = bb_vm_istring(vm, "coro", 4);
	ci_map_put(vm->globals, coro_key, (ci_ptr)coro_ns);
	ci_dec((ci_ptr)coro_ns);
}
