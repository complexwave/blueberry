/* callapi.c — test/demo natives for bb_coro_call API */

/* callproxy(fn, a, b, c) — call fn with up to 3 args, return result */
static ci_ptr bb_native_callproxy(bb_coro *c, ci_ptr_arg self, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	if (!CI_IS_CLOSURE(a0))
		bb_coro_error(c, "callproxy: first arg must be a function");

	bb_closure *cl = (bb_closure *)a0;
	return bb_coro_call(c, cl, a1, a2, NULL);
}

/* callproxy_var(fn, args_array) — call fn with array of args, return results */
static bb_var_ret bb_native_callproxy_var(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self;
	if (!nargs || !CI_IS_CLOSURE(args[0]))
		bb_coro_error(c, "callproxy_var: first arg must be a function");

	bb_closure *cl = (bb_closure *)args[0];

	ci_ptr call_args[32] = {};
	uint32_t call_nargs = 0;

	if (nargs > 1 && CI_IS_ANY_ARR(args[1])) {
		ci_array *arr = (ci_array *)args[1];
		call_nargs = ci_arr_len(arr);
		if (call_nargs > 32) call_nargs = 32;
		for (uint32_t i = 0; i < call_nargs; i++)
			call_args[i] = ci_arr_index(arr, i);
	}

	ci_ptr call_rets[32] = {};
	bb_coro_call_var(c, cl, call_args, call_nargs, call_rets, 32);

	for (uint32_t i = 0; i < 32; i++) {
		BB_PUSH_RET(call_rets[i]);
	}
	return nargs;
}

/* varrets_example() — demo: returns 10, 20, 30 via BB_PUSH_RET */
static bb_var_ret bb_native_varrets_example(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)c; (void)self;
	BB_PUSH_RET(CI_PACKINT(10));
	BB_PUSH_RET(CI_PACKINT(20));
	BB_PUSH_RET(CI_PACKINT(30));
	return nargs;
}

/* varrets_return(a) — demo: returns a, a+1, a+2 via BB_RETURN */
static bb_var_ret bb_native_varrets_return(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)c; (void)self;
	intptr_t a = nargs > 0 ? CI_INT(args[0]) : 0;
	BB_RETURN(CI_PACKINT(a), CI_PACKINT(a + 1), CI_PACKINT(a + 2));
}

static void bb_lib_callapi_init(bb_vm *vm) {
	bb_closure *cl;

	cl = bb_vm_native(vm, "callproxy", bb_native_callproxy);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);

	cl = bb_vm_native_var(vm, "callproxy_var", bb_native_callproxy_var);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);

	cl = bb_vm_native_var(vm, "varrets_example", bb_native_varrets_example);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);

	cl = bb_vm_native_var(vm, "varrets_return", bb_native_varrets_return);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);
}
