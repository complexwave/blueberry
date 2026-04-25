/*
 * blueberry_vm/proto_string.c — string built-in prototype methods
 *
 * All methods receive self (the string) as a0.
 */

/* ---- query ---- */

static ci_ptr bb_str_len(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	return CI_PACKINT((intptr_t)ci_str_len(s));
}

static ci_ptr bb_str_size(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	return CI_PACKINT((intptr_t)ci_str_size(s));
}

/* ---- access ---- */

/* at(idx) — single byte as int, null if OOB */
static ci_ptr bb_str_at(bb_vm_arg *vm, ci_ptr s, ci_ptr idx) {
	BB_CHECK_STRING(s);
	BB_CHECK_INT(idx);
	intptr_t i = CI_INT(idx);
	size_t len = ci_str_len(s);
	if (i < 0 || (size_t)i >= len)
		return NULL;
	return CI_PACKINT(ci_str_head(s)[i]);
}

/* slice(start, end) — new string from [start, end), negative = from end */
static ci_ptr bb_str_slice(bb_vm_arg *vm, ci_ptr s, ci_ptr a_start, ci_ptr a_end) {
	BB_CHECK_STRING(s);
	BB_CHECK_INT(a_start);

	intptr_t len = (intptr_t)ci_str_len(s);
	intptr_t si = CI_INT(a_start);
	intptr_t ei;

	if (a_end && CI_IS_INT(a_end))
		ei = CI_INT(a_end);
	else
		ei = len;

	/* negative index wraps */
	if (si < 0) si += len;
	if (ei < 0) ei += len;

	/* clamp */
	if (si < 0) si = 0;
	if (ei > len) ei = len;
	if (si >= ei)
		return (ci_ptr)ci_str_from_cstr("");

	uint8_t *head = ci_str_head(s);
	size_t slen = (size_t)(ei - si);

	ci_str *r = ci_str_new(slen);
	if (!r) bb_error("slice: out of memory");
	uint8_t *dst = ci_str_ensure_tail(r, slen);
	memcpy(dst, head + si, slen);
	ci_str_put_tail(r, slen);
	return (ci_ptr)r;
}

/* ---- search ---- */

/* find(needle) — index of first occurrence, null if not found */
static ci_ptr bb_str_find(bb_vm_arg *vm, ci_ptr s, ci_ptr needle) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(needle);

	uint8_t *h = ci_str_head(s);
	size_t hlen = ci_str_len(s);
	uint8_t *n = ci_str_head(needle);
	size_t nlen = ci_str_len(needle);

	if (nlen == 0)
		return CI_PACKINT(0);
	if (nlen > hlen)
		return CI_BOOL(0);

	for (size_t i = 0; i <= hlen - nlen; i++) {
		if (memcmp(h + i, n, nlen) == 0)
			return CI_PACKINT((intptr_t)i);
	}
	return CI_BOOL(0);
}

/* contains(needle) — true or false */
static ci_ptr bb_str_contains(bb_vm_arg *vm, ci_ptr s, ci_ptr needle) {
	ci_ptr idx = bb_str_find(vm, s, needle);
	return CI_BOOL(idx != CI_BOOL(0));
}

/* starts(prefix) */
static ci_ptr bb_str_starts(bb_vm_arg *vm, ci_ptr s, ci_ptr prefix) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(prefix);

	size_t slen = ci_str_len(s);
	size_t plen = ci_str_len(prefix);
	if (plen > slen)
		return CI_PACKINT(0);
	return CI_PACKINT(memcmp(ci_str_head(s), ci_str_head(prefix), plen) == 0);
}

/* ends(suffix) */
static ci_ptr bb_str_ends(bb_vm_arg *vm, ci_ptr s, ci_ptr suffix) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(suffix);

	size_t slen = ci_str_len(s);
	size_t xlen = ci_str_len(suffix);
	if (xlen > slen)
		return CI_PACKINT(0);
	return CI_PACKINT(memcmp(ci_str_head(s) + slen - xlen, ci_str_head(suffix), xlen) == 0);
}

/* ---- mutation (return self for chaining) ---- */

/* append(other) — append string to self */
static ci_ptr bb_str_append(bb_vm_arg *vm, ci_ptr s, ci_ptr other) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(other);

	size_t olen = ci_str_len(other);
	if (olen == 0)
		return s;

	/* upgrade small string if needed */
	if (CI_IS_STR_SMALL(s)) {
		ci_str *upgraded = ci_str_upgrade(s);
		if (!upgraded) bb_error("append: cannot upgrade small string");
		s = (ci_ptr)upgraded;
	}

	if (!ci_str_append((ci_str *)s, ci_str_head(other), olen))
		bb_error("append: out of memory");
	return s;
}

/* prepend(other) */
static ci_ptr bb_str_prepend(bb_vm_arg *vm, ci_ptr s, ci_ptr other) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(other);

	size_t olen = ci_str_len(other);
	if (olen == 0)
		return s;

	if (CI_IS_STR_SMALL(s)) {
		ci_str *upgraded = ci_str_upgrade(s);
		if (!upgraded) bb_error("prepend: cannot upgrade small string");
		s = (ci_ptr)upgraded;
	}

	if (!ci_str_prepend((ci_str *)s, ci_str_head(other), olen))
		bb_error("prepend: out of memory");
	return s;
}

/* clear() */
static ci_ptr bb_str_clear(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	ci_str_clear((ci_str *)s);
	return s;
}

/* ---- produce new strings ---- */

/* copy() — deep copy */
static ci_ptr bb_str_copy(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	ci_str *r = ci_str_copy(s, 0);
	if (!r) bb_error("copy: out of memory");
	return (ci_ptr)r;
}

/* upper() — new uppercased string */
static ci_ptr bb_str_upper(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	size_t len = ci_str_len(s);
	ci_str *r = ci_str_copy(s, 0);
	if (!r) bb_error("upper: out of memory");
	uint8_t *p = ci_str_head(r);
	for (size_t i = 0; i < len; i++) {
		if (p[i] >= 'a' && p[i] <= 'z')
			p[i] -= 32;
	}
	ci_str_reset_hash(r);
	return (ci_ptr)r;
}

/* lower() — new lowercased string */
static ci_ptr bb_str_lower(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	size_t len = ci_str_len(s);
	ci_str *r = ci_str_copy(s, 0);
	if (!r) bb_error("lower: out of memory");
	uint8_t *p = ci_str_head(r);
	for (size_t i = 0; i < len; i++) {
		if (p[i] >= 'A' && p[i] <= 'Z')
			p[i] += 32;
	}
	ci_str_reset_hash(r);
	return (ci_ptr)r;
}

/* trim() — new string with leading/trailing whitespace removed */
static ci_ptr bb_str_trim(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	uint8_t *h = ci_str_head(s);
	size_t len = ci_str_len(s);

	size_t start = 0;
	while (start < len && (h[start] == ' ' || h[start] == '\t' || h[start] == '\n' || h[start] == '\r'))
		start++;

	size_t end = len;
	while (end > start && (h[end-1] == ' ' || h[end-1] == '\t' || h[end-1] == '\n' || h[end-1] == '\r'))
		end--;

	size_t rlen = end - start;
	ci_str *r = ci_str_new(rlen);
	if (!r) bb_error("trim: out of memory");
	uint8_t *dst = ci_str_ensure_tail(r, rlen);
	memcpy(dst, h + start, rlen);
	ci_str_put_tail(r, rlen);
	return (ci_ptr)r;
}

/* rev() — new reversed string */
static ci_ptr bb_str_rev(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	size_t len = ci_str_len(s);
	ci_str *r = ci_str_new(len);
	if (!r) bb_error("rev: out of memory");
	uint8_t *src = ci_str_head(s);
	uint8_t *dst = ci_str_ensure_tail(r, len);
	for (size_t i = 0; i < len; i++)
		dst[i] = src[len - 1 - i];
	ci_str_put_tail(r, len);
	return (ci_ptr)r;
}

/* repeat(n) — new string, self repeated n times */
static ci_ptr bb_str_repeat(bb_vm_arg *vm, ci_ptr s, ci_ptr n) {
	BB_CHECK_STRING(s);
	BB_CHECK_INT(n);
	intptr_t count = CI_INT(n);
	if (count <= 0)
		return (ci_ptr)ci_str_from_cstr("");

	size_t len = ci_str_len(s);
	size_t total = len * (size_t)count;
	ci_str *r = ci_str_new(total);
	if (!r) bb_error("repeat: out of memory");
	uint8_t *src = ci_str_head(s);
	uint8_t *dst = ci_str_ensure_tail(r, total);
	for (intptr_t i = 0; i < count; i++) {
		memcpy(dst, src, len);
		dst += len;
	}
	ci_str_put_tail(r, total);
	return (ci_ptr)r;
}

/* ---- conversion ---- */

/* split(delim) — array of strings split on delimiter */
static ci_ptr bb_str_split(bb_vm_arg *vm, ci_ptr s, ci_ptr delim) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(delim);

	uint8_t *h = ci_str_head(s);
	size_t hlen = ci_str_len(s);
	uint8_t *d = ci_str_head(delim);
	size_t dlen = ci_str_len(delim);

	ci_array *arr = ci_arr_new(8);
	if (!arr) bb_error("split: out of memory");

	if (dlen == 0) {
		/* split each byte */
		for (size_t i = 0; i < hlen; i++) {
			ci_str *part = ci_str_new(1);
			if (!part) bb_error("split: out of memory");
			uint8_t *dst = ci_str_ensure_tail(part, 1);
			*dst = h[i];
			ci_str_put_tail(part, 1);
			ci_arr_push(arr, (ci_ptr)part);
		}
		return (ci_ptr)arr;
	}

	size_t prev = 0;
	for (size_t i = 0; i <= hlen - dlen; i++) {
		if (memcmp(h + i, d, dlen) == 0) {
			size_t plen = i - prev;
			ci_str *part = ci_str_new(plen);
			if (!part) bb_error("split: out of memory");
			uint8_t *dst = ci_str_ensure_tail(part, plen);
			memcpy(dst, h + prev, plen);
			ci_str_put_tail(part, plen);
			ci_arr_push(arr, (ci_ptr)part);
			prev = i + dlen;
			i += dlen - 1;
		}
	}
	/* trailing piece */
	size_t plen = hlen - prev;
	ci_str *part = ci_str_new(plen);
	if (!part) bb_error("split: out of memory");
	uint8_t *dst = ci_str_ensure_tail(part, plen);
	memcpy(dst, h + prev, plen);
	ci_str_put_tail(part, plen);
	ci_arr_push(arr, (ci_ptr)part);

	return (ci_ptr)arr;
}

/* bytes() — array of ints (byte values) */
static ci_ptr bb_str_bytes(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	size_t len = ci_str_len(s);
	uint8_t *h = ci_str_head(s);

	ci_array *arr = ci_arr_new((uint32_t)(len < 8 ? 8 : len));
	if (!arr) bb_error("bytes: out of memory");

	for (size_t i = 0; i < len; i++)
		ci_arr_push(arr, CI_PACKINT(h[i]));
	return (ci_ptr)arr;
}

/* hash() — FNV-1a hash as int */
static ci_ptr bb_str_hash(bb_vm_arg *vm, ci_ptr s) {
	BB_CHECK_STRING(s);
	return CI_PACKINT((intptr_t)ci_str_hash((ci_str *)s));
}

/* eq(other) — string equality */
static ci_ptr bb_str_eq(bb_vm_arg *vm, ci_ptr s, ci_ptr other) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(other);
	return CI_PACKINT(ci_str_eq((const ci_str *)s, (const ci_str *)other));
}

/* cmp(other) — lexicographic compare: -1/0/1 */
static ci_ptr bb_str_cmp(bb_vm_arg *vm, ci_ptr s, ci_ptr other) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(other);

	size_t la = ci_str_len(s);
	size_t lb = ci_str_len(other);
	size_t min = la < lb ? la : lb;
	int c = memcmp(ci_str_head(s), ci_str_head(other), min);
	if (c == 0) {
		if (la < lb) c = -1;
		else if (la > lb) c = 1;
	} else {
		c = c < 0 ? -1 : 1;
	}
	return CI_PACKINT(c);
}

/* ---- replace ---- */

/* replace(needle, replacement) — new string with all occurrences replaced */
static ci_ptr bb_str_replace(bb_vm_arg *vm, ci_ptr s, ci_ptr needle, ci_ptr replacement) {
	BB_CHECK_STRING(s);
	BB_CHECK_STRING(needle);
	BB_CHECK_STRING(replacement);

	uint8_t *h = ci_str_head(s);
	size_t hlen = ci_str_len(s);
	uint8_t *n = ci_str_head(needle);
	size_t nlen = ci_str_len(needle);
	uint8_t *r = ci_str_head(replacement);
	size_t rlen = ci_str_len(replacement);

	if (nlen == 0)
		return (ci_ptr)ci_str_copy(s, 0);

	/* first pass: count occurrences */
	size_t count = 0;
	for (size_t i = 0; i <= hlen - nlen; i++) {
		if (memcmp(h + i, n, nlen) == 0) {
			count++;
			i += nlen - 1;
		}
	}
	if (count == 0)
		return (ci_ptr)ci_str_copy(s, 0);

	size_t newlen = hlen - count * nlen + count * rlen;
	ci_str *out = ci_str_new(newlen);
	if (!out) bb_error("replace: out of memory");

	uint8_t *dst = ci_str_ensure_tail(out, newlen);
	size_t prev = 0;
	for (size_t i = 0; i <= hlen - nlen; i++) {
		if (memcmp(h + i, n, nlen) == 0) {
			memcpy(dst, h + prev, i - prev);
			dst += i - prev;
			memcpy(dst, r, rlen);
			dst += rlen;
			prev = i + nlen;
			i += nlen - 1;
		}
	}
	memcpy(dst, h + prev, hlen - prev);
	ci_str_put_tail(out, newlen);
	return (ci_ptr)out;
}

/* ---- registration ---- */

static void bb_proto_string_init(bb_vm *vm) {
	static const bb_cfunc str_lib[] = {
		/* query */
		{ "len",      bb_str_len,      0 },
		{ "size",     bb_str_size,     0 },
		/* access */
		{ "at",       bb_str_at,       0 },
		{ "slice",    bb_str_slice,    0 },
		/* search */
		{ "find",     bb_str_find,     0 },
		{ "contains", bb_str_contains, 0 },
		{ "starts",   bb_str_starts,   0 },
		{ "ends",     bb_str_ends,     0 },
		/* mutation */
		{ "append",   bb_str_append,   0 },
		{ "prepend",  bb_str_prepend,  0 },
		{ "clear",    bb_str_clear,    0 },
		/* produce new */
		{ "copy",     bb_str_copy,     0 },
		{ "upper",    bb_str_upper,    0 },
		{ "lower",    bb_str_lower,    0 },
		{ "trim",     bb_str_trim,     0 },
		{ "rev",      bb_str_rev,      0 },
		{ "repeat",   bb_str_repeat,   0 },
		{ "replace",  bb_str_replace,  0 },
		/* conversion */
		{ "split",    bb_str_split,    0 },
		{ "bytes",    bb_str_bytes,    0 },
		/* compare */
		{ "hash",     bb_str_hash,     0 },
		{ "eq",       bb_str_eq,       0 },
		{ "cmp",      bb_str_cmp,      0 },
	};
	vm->proto_string = ci_map_ident_new(32);
	bb_func2map(vm, vm->proto_string, str_lib, sizeof(str_lib) / sizeof(str_lib[0]));
}
