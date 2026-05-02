/* callapi.c — test/demo natives for bb_coro_call API */

/* callproxy(fn, a, b, c) — call fn with up to 3 args, return result */
static ci_ptr bb_native_callproxy(bb_coro *c, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	if (!CI_IS_CLOSURE(a0))
		bb_coro_error(c, "callproxy: first arg must be a function");

	bb_closure *cl = (bb_closure *)a0;
	return bb_coro_call(c, cl, a1, a2, NULL);
}

/* callproxy_var(fn, args_array) — call fn with array of args, return first result */
static ci_ptr bb_native_callproxy_var(bb_coro *c, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)a2;
	if (!a0 || !CI_IS_CLOSURE(a0))
		bb_coro_error(c, "callproxy_var: first arg must be a function");

	bb_closure *cl = (bb_closure *)a0;

	ci_ptr args[32] = {};
	uint32_t nargs = 0;

	if (CI_IS_ANY_ARR(a1)) {
		ci_array *arr = (ci_array *)a1;
		nargs = ci_arr_len(arr);
		if (nargs > 32) nargs = 32;
		for (uint32_t i = 0; i < nargs; i++)
			args[i] = ci_arr_index(arr, i);
	}

	ci_ptr rets[32] = {};
	bb_coro_call_var(c, cl, args, nargs, rets, 32);

	ci_ptr result = rets[0];
	/* don't finalize — caller owns the values */
	return result;
}

static void bb_lib_callapi_init(bb_vm *vm) {
	bb_closure *cl;

	cl = bb_vm_native(vm, "callproxy", bb_native_callproxy);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);

	cl = bb_vm_native(vm, "callproxy_var", bb_native_callproxy_var);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);
}
