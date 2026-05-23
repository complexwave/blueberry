/*
 * blueberry_vm/proto_array.c — array built-in prototype methods
 *
 * All methods receive self (the array) as a0.
 */

static ci_ptr bb_arr_len(bb_coro_arg *c, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return CI_PACKINT(ci_arr_len((ci_array *)arr));
}

static ci_ptr bb_arr_push(bb_coro_arg *c, ci_ptr arr, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	ci_inc(val);
	ci_arr_push((ci_array *)arr, val);
	return NULL;
}

static ci_ptr bb_arr_pop(bb_coro_arg *c, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return ci_arr_pop((ci_array *)arr);
}

static ci_ptr bb_arr_shift(bb_coro_arg *c, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return ci_arr_shift((ci_array *)arr);
}

static ci_ptr bb_arr_unshift(bb_coro_arg *c, ci_ptr arr, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	ci_inc(val);
	ci_arr_unshift((ci_array *)arr, val);
	return NULL;
}

static ci_ptr bb_arr_size(bb_coro_arg *c, ci_ptr arr, ci_ptr newsize) {
	BB_CHECK_ARRAY(arr);

	if (newsize != NULL) {
		BB_CHECK_INT(newsize);
		ci_array *a = (ci_array *)arr;
		intptr_t ns = CI_INT(newsize);
		if (ns < 0)
			bb_coro_error(c, "size: must be non-negative");

		uint32_t oldlen = ci_arr_len(a);
		if ((uint32_t)ns > oldlen) {
			ci_arr_ensure_space(a, (uint32_t)ns - oldlen);
		} else {
			while (ci_arr_len(a) > (uint32_t)ns)
				ci_arr_pop(a);
		}
		return CI_PACKINT(a->size);
	}

	return CI_PACKINT(((ci_array *)arr)->size);
}

static ci_ptr bb_arr__offset(bb_coro_arg *c, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return CI_PACKINT(((ci_array *)arr)->offset);
}

static ci_ptr bb_arr_find(bb_coro_arg *c, ci_ptr arr, ci_ptr val, ci_ptr start) {
	BB_CHECK_ARRAY(arr);
	uint32_t from = 0;
	if (CI_IS_INT(start)) {
		intptr_t s = CI_INT(start);
		if (s > 0) from = (uint32_t)s;
	}
	int32_t idx = ci_arr_find((ci_array *)arr, val, from);
	if (idx < 0) return CI_BOOL(0);
	return CI_PACKINT(idx);
}

static ci_ptr bb_arr_contains(bb_coro_arg *c, ci_ptr arr, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	return CI_BOOL(ci_arr_contains((ci_array *)arr, val));
}

static ci_ptr bb_arr_slice(bb_coro_arg *c, ci_ptr arr, ci_ptr from, ci_ptr to) {
	BB_CHECK_ARRAY(arr);
	BB_CHECK_INT(from);
	int32_t to_val = CI_IS_INT(to) ? (int32_t)CI_INT(to) : (int32_t)ci_arr_len((ci_array *)arr);
	ci_array *result = ci_arr_slice((ci_array *)arr, (int32_t)CI_INT(from), to_val);
	return (ci_ptr)result;
}

static ci_ptr bb_arr_copy(bb_coro_arg *c, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	return (ci_ptr)ci_arr_copy((ci_array *)arr);
}

static ci_ptr bb_arr_clear(bb_coro_arg *c, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	ci_arr_clear((ci_array *)arr);
	return NULL;
}

static ci_ptr bb_arr_reverse(bb_coro_arg *c, ci_ptr arr) {
	BB_CHECK_ARRAY(arr);
	ci_arr_reverse((ci_array *)arr);
	return NULL;
}

static ci_ptr bb_arr_get(bb_coro_arg *c, ci_ptr arr, ci_ptr idx) {
	BB_CHECK_ARRAY(arr);
	BB_CHECK_INT(idx);
	const ci_array *a = (const ci_array *)arr;
	uint32_t i = ci_arr_wrapindex(a, CI_INT(idx));
	if (i >= ci_arr_len(a))
		return NULL;
	return ci_arr_index(a, i);
}

static ci_ptr bb_arr_set(bb_coro_arg *c, ci_ptr arr, ci_ptr idx, ci_ptr val) {
	BB_CHECK_ARRAY(arr);
	BB_CHECK_INT(idx);
	ci_array *a = (ci_array *)arr;
	uint32_t i = ci_arr_wrapindex(a, CI_INT(idx));
	if (i >= ci_arr_len(a)){
		if(i >= INT32_MAX) return NULL;

		ci_arr_extend(a, i + 1);
	}

	ci_inc(val);
	ci_ptr old = ci_arr_index(a, i);
	ci_dec(old);
	ci_arr_set(a, i, val);
	return NULL;
}

static bb_var_ret bb_arr_merge(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)nrets;
	BB_CHECK_ARRAY(self);

	ci_array *dst = (ci_array *)self;

	/* pass 1: sum total elements, check if all srcs are non-wrapped */
	uint32_t total = 0;
	int all_contiguous = 1;
	for (size_t i = 0; i < nargs; i++) {
		if (!CI_IS_ANY_ARR(args[i]))
			bb_coro_error(c, "merge: argument is not an array");

		ci_array *src = (ci_array *)args[i];
		BB_NO_OVERFLOW_ADD(total, src->length, total);

		if (src->length > 0 && (src->offset + src->length) > src->size)
			all_contiguous = 0;
	}

	if (total == 0) {
		BB_PUSH_RET((ci_ptr)dst);
		return nargs;
	}

	/* ensure space — may linearize dst (sets offset to 0) */
	ci_arr_ensure_space(dst, total);

	/* fastpath: dst offset==0 and all srcs contiguous → memcpy */
	if (dst->offset == 0 && all_contiguous) {

		for (size_t i = 0; i < nargs; i++) {
			ci_array *src = (ci_array *)args[i];

			if (src->length == 0) continue;

			memcpy(dst->data + dst->length,
			       src->data + src->offset,
			       src->length * sizeof(ci_ptr));
			dst->length += src->length;
		}

	} else {
		/* slowpath: element-by-element through circular index */
		for (size_t i = 0; i < nargs; i++) {
			ci_array *src = (ci_array *)args[i];

			for (uint32_t j = 0; j < src->length; j++) {
				size_t dst_idx = ci_arr__idx(dst, dst->length);
				size_t src_idx = ci_arr__idx(src, j);

				dst->data[dst_idx] = src->data[src_idx];
				dst->length++;
			}
		}
	}

	/* TODO: ci_inc each copied element when refcounting */
	BB_PUSH_RET((ci_ptr)dst);
	return nargs;
}

/* core splice: operates on raw pointer + count for inserts. no malloc. */
static ci_ptr bb_arr__splice(bb_coro_arg *c, ci_array *a,
                             intptr_t raw_start, intptr_t del_count,
                             uint32_t insert_count, ci_ptr *inserts) {
	uint32_t len = a->length;

	/* clamp start to [0, len] */
	uint32_t start;
	if (raw_start < 0) start = 0;
	else if ((uint32_t)raw_start > len) start = len;
	else start = (uint32_t)raw_start;

	/* clamp delete to not exceed past end */
	if (del_count < 0) del_count = 0;
	uint32_t tail_idx = start + (uint32_t)del_count;
	if (tail_idx > len) tail_idx = len;
	uint32_t actual_del = tail_idx - start;

	if (insert_count > (uint32_t)INT32_MAX)
		bb_coro_error(c, "splice: too many inserts");

	int32_t size_diff = (int32_t)insert_count - (int32_t)actual_del;

	/* grow if inserting more than deleting */
	if (size_diff > 0)
		ci_arr_ensure_space(a, (uint32_t)size_diff);

	/* linearize — no wraparound after this */
	ci_arr_ensure_continuous(a);

	ci_ptr *base = a->data + a->offset;

	/* ci_dec elements being overwritten */
	for (uint32_t i = start; i < tail_idx; i++)
		ci_dec(base[i]);

	/* memmove tail: [tail_idx .. len) → [tail_idx + size_diff .. ] */
	uint32_t tail_len = len - tail_idx;
	if (tail_len > 0 && size_diff != 0) {
		ci_ptr *src = base + tail_idx;
		ci_ptr *dst = base + tail_idx + size_diff;

		if (dst < base) return NULL;

		memmove(dst, src, tail_len * sizeof(ci_ptr));
	}
	
	/* copy inserts into the gap */
	for (uint32_t i = 0; i < insert_count; i++) {
		ci_inc(inserts[i]);
		base[start + i] = inserts[i];
	}

	uint32_t new_len = (uint32_t)((int32_t)len + size_diff);

	/* null out stale tail slots when shrinking */
	for (uint32_t i = new_len; i < len; i++)
		base[i] = NULL;

	a->length = new_len;
	return (ci_ptr)a;
}

/* splice(start, deleteCount, ...inserts) — vararg */
static bb_var_ret bb_arr_splice(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)nrets;
	BB_CHECK_ARRAY(self);
	if (nargs < 2)
		bb_coro_error(c, "splice: need at least start and deleteCount");
	BB_CHECK_INT(args[0]);
	BB_CHECK_INT(args[1]);

	uint32_t ins_n = (nargs > 2) ? (uint32_t)(nargs - 2) : 0;
	BB_PUSH_RET(bb_arr__splice(c, (ci_array *)self,
	                      CI_INT(args[0]), CI_INT(args[1]),
	                      ins_n, args + 2));
	return nargs;
}

/* splice_arr(start, deleteCount, insertArray) — vararg, reads array from args[2] */
static bb_var_ret bb_arr_splice_arr(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)nrets;
	BB_CHECK_ARRAY(self);
	if (nargs < 2)
		bb_coro_error(c, "splice_arr: need at least start and deleteCount");
	BB_CHECK_INT(args[0]);
	BB_CHECK_INT(args[1]);

	ci_ptr *head = NULL;
	uint32_t n = 0;
	if (nargs >= 3 && args[2] != NULL && CI_IS_ANY_ARR(args[2])) {
		ci_array *src = (ci_array *)args[2];
		n = src->length;
		if (n > 0) {
			ci_arr_ensure_continuous(src);
			head = src->data + src->offset;
		}
	}

	BB_PUSH_RET(bb_arr__splice(c, (ci_array *)self,
	                      CI_INT(args[0]), CI_INT(args[1]), n, head));
	return nargs;
}

/* ---- registration ---- */

static void bb_proto_array_init(bb_vm *vm) {
	static const bb_cfunc arr_lib[] = {
		{ "len",     bb_arr_len,     0 },
		{ "push",    bb_arr_push,    0 },
		{ "pop",     bb_arr_pop,     0 },
		{ "shift",   bb_arr_shift,   0 },
		{ "unshift", bb_arr_unshift, 0 },
		{ "size",    bb_arr_size,    0 },
		{ "find",     bb_arr_find,     0 },
		{ "contains",bb_arr_contains, 0 },
		{ "slice",   bb_arr_slice,   0 },
		{ "copy",    bb_arr_copy,    0 },
		{ "clear",   bb_arr_clear,   0 },
		{ "reverse", bb_arr_reverse, 0 },
		{ "get",     bb_arr_get,     0 },
		{ "set",     bb_arr_set,     0 },
		{ "merge",   bb_arr_merge,   BB_FN_NATIVE_VAR },
		{ "splice",     bb_arr_splice,     BB_FN_NATIVE_VAR },
		{ "splice_arr", bb_arr_splice_arr, BB_FN_NATIVE_VAR },
		{ "_offset", bb_arr__offset, 0 },
	};
	ci_map *proto = bb_proto_register(vm, "array");
	bb_func2map(vm, proto, arr_lib, sizeof(arr_lib) / sizeof(arr_lib[0]));

	bb_set_arena_prototype(CI_ARR,            proto);
	bb_set_arena_prototype(CI_ARR_SMALL_128,  proto);
	bb_set_arena_prototype(CI_ARR_SMALL_256,  proto);
	bb_set_arena_prototype(CI_ARR_SMALL_1024, proto);
	bb_set_arena_prototype(CI_ARR_SMALL_2048, proto);
}
