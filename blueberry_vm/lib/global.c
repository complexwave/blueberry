/*
 * blueberry_vm/lib/global.c — built-in global functions (print, require, etc.)
 */

static void bb_print_val(ci_ptr v) {
	if (!v)
		printf("null");
	else if (CI_IS_INT(v))
		printf("%ld", CI_INT(v));
	else if (CI_IS_BOOL(v))
		printf("%s", v == CI_BOOL(1) ? "true" : "false");
	else if (CI_IS_ANY_STR(v))
		printf("%.*s", (int)ci_str_len(v), (char *)ci_str_head(v));
	else if (CI_IS_NUMBER(v)) {
		printf("<number:%p:", (void *)v);
		ci_number_print(v);
		printf(">");
	}
	else
		printf("<obj:%p>", (void *)v);
}

static bb_var_ret bb_native_print(bb_coro *c, ci_ptr self, size_t n, ci_ptr *args, size_t nrets) {
	(void)c; (void)self; (void)nrets;
	for(size_t i = 0; i < n; i++){
		bb_print_val(args[i]);
		printf(" ");
	}
	printf("\n");
	return n;
}

static ci_ptr bb_native_require(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	if (!CI_IS_ANY_STR(a0))
		bb_coro_error(c, "require: argument must be a string");

	char path[1024];
	size_t len = ci_str_len(a0);
	if (len >= sizeof(path))
		bb_coro_error(c, "require: path too long");
	memcpy(path, ci_str_head(a0), len);
	path[len] = '\0';

#ifndef BB_CBC_ONLY
	uint32_t blen;
#ifdef CI_DEBUG_NOFREE
	ci_never_free = 1;
#endif
	uint8_t *buf = bb_compile_ci_file(path, &blen);
#ifdef CI_DEBUG_NOFREE
	ci_never_free = 0;
#endif
	if (!buf)
		bb_coro_error(c, "require: cannot compile '%s'", path);

	bb_unit *unit = bb_vm_loadbytecode(c->vm, buf, blen);
	free(buf);

	bb_function *main_fn = (bb_function *)ci_arr_index(unit->functions, 0);
	ci_inc(main_fn);
	bb_closure *cl = bb_vm_closure(c->vm, main_fn);
	BB_RETURN_NOINC(cl);
#else
	bb_coro_error(c, "require: not supported in cbc-only mode");
#endif
}

static ci_ptr bb_native_stacktrace(bb_coro *c, ci_ptr_arg self, ci_ptr a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	int dumpregs = 0;
	if (CI_IS_INT(a0))
		dumpregs = (int)CI_INT(a0);
	bb_coro_dump_stack(c, dumpregs);
	return NULL;
}

/* ---- registration ---- */

static void bb_lib_global_init(bb_vm *vm) {
	bb_func2global(vm, "print",      bb_native_print,      BB_FN_NATIVE_VAR);
	bb_func2global(vm, "stacktrace", bb_native_stacktrace, 0);
	bb_func2global(vm, "require",    bb_native_require,    0);
}
