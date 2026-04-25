/*
 * blueberry_vm/proto_array.c — array built-in prototype methods
 *
 * All methods receive self (the array) as a0.
 */

static ci_ptr bb_arr_len(bb_vm_arg *vm, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return CI_PACKINT(ci_arr_len((ci_array *)arr));
}

static ci_ptr bb_arr_push(bb_vm_arg *vm, ci_ptr arr, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	ci_inc(val);
	ci_arr_push((ci_array *)arr, val);
	return NULL;
}

static ci_ptr bb_arr_pop(bb_vm_arg *vm, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return ci_arr_pop((ci_array *)arr);
}

static ci_ptr bb_arr_shift(bb_vm_arg *vm, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return ci_arr_shift((ci_array *)arr);
}

static ci_ptr bb_arr_unshift(bb_vm_arg *vm, ci_ptr arr, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	ci_inc(val);
	ci_arr_unshift((ci_array *)arr, val);
	return NULL;
}

static ci_ptr bb_arr_resize(bb_vm_arg *vm, ci_ptr arr, ci_ptr newsize) {
	BB_CHECK_ARRAY(arr);
	BB_CHECK_INT(newsize);

	ci_array *a = (ci_array *)arr;
	intptr_t newlen = CI_INT(newsize);
	if (newlen < 0)
		bb_error("resize: size must be non-negative");

	uint32_t oldlen = ci_arr_len(a);
	if ((uint32_t)newlen > oldlen) {
		ci_arr_ensure_space(a, (uint32_t)newlen - oldlen);
		for (uint32_t i = oldlen; i < (uint32_t)newlen; i++)
			ci_arr_push(a, NULL);
	} else {
		while (ci_arr_len(a) > (uint32_t)newlen)
			ci_arr_pop(a);
	}
	
	return CI_PACKINT(a->size);
}

static ci_ptr bb_arr_size(bb_vm_arg *vm, ci_ptr arr, ci_ptr newsize) {
	BB_CHECK_ARRAY(arr);
	
	if(newsize != NULL){
		return bb_arr_resize(vm, arr, newsize);
	}
	
	return CI_PACKINT(((ci_array *)arr)->size);
}

static ci_ptr bb_arr__offset(bb_vm_arg *vm, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return CI_PACKINT(((ci_array *)arr)->offset);
}

static ci_ptr bb_arr_find(bb_vm_arg *vm, ci_ptr arr, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	int32_t idx = ci_arr_find((ci_array *)arr, val, 0);
	if (idx < 0) return CI_BOOL(0);
	return CI_PACKINT(idx);
}

static ci_ptr bb_arr_contains(bb_vm_arg *vm, ci_ptr arr, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	return CI_BOOL(ci_arr_contains((ci_array *)arr, val));
}

static ci_ptr bb_arr_slice(bb_vm_arg *vm, ci_ptr arr, ci_ptr from, ci_ptr to) {
	BB_CHECK_ARRAY(arr);
	BB_CHECK_INT(from);
	BB_CHECK_INT(to);
	ci_array *result = ci_arr_slice((ci_array *)arr, (int32_t)CI_INT(from), (int32_t)CI_INT(to));
	return (ci_ptr)result;
}

static ci_ptr bb_arr_copy(bb_vm_arg *vm, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return (ci_ptr)ci_arr_copy((ci_array *)arr);
}

static ci_ptr bb_arr_clear(bb_vm_arg *vm, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	ci_arr_clear((ci_array *)arr);
	return NULL;
}

static ci_ptr bb_arr_reverse(bb_vm_arg *vm, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	ci_arr_reverse((ci_array *)arr);
	return NULL;
}

/* ---- registration ---- */

static void bb_proto_array_init(bb_vm *vm) {
	static const bb_cfunc arr_lib[] = {
		{ "len",     bb_arr_len,     0 },
		{ "push",    bb_arr_push,    0 },
		{ "pop",     bb_arr_pop,     0 },
		{ "shift",   bb_arr_shift,   0 },
		{ "unshift", bb_arr_unshift, 0 },
		{ "resize",  bb_arr_resize,  0 },
		{ "size",    bb_arr_size,    0 },
		{ "find",     bb_arr_find,     0 },
		{ "contains",bb_arr_contains, 0 },
		{ "slice",   bb_arr_slice,   0 },
		{ "copy",    bb_arr_copy,    0 },
		{ "clear",   bb_arr_clear,   0 },
		{ "reverse", bb_arr_reverse, 0 },
		{ "_offset", bb_arr__offset, 0 },
	};
	vm->proto_array = ci_map_ident_new(16);
	bb_func2map(vm, vm->proto_array, arr_lib, sizeof(arr_lib) / sizeof(arr_lib[0]));
}
