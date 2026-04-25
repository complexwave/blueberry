#include "cma.h"
#include <ctype.h>

// Define CMA_USE_SIMD=1 to enable SSSE3 rep_set path
#if CMA_USE_SIMD && defined(__SSSE3__)
#include <tmmintrin.h>
#else
#undef CMA_USE_SIMD
#endif

// ================================================================
//  match functions — return 1 on match, 0 on miss
//  contract: on miss, pos is unchanged (op backtracks internally)
// ================================================================

CMA_OP cma_match_noop(cma_state *s, cma_op *op) {
	(void)op;
	return cma_ret_matched(s);
}

CMA_OP cma_match_endl(cma_state *s, cma_op *op) {
	(void)op;
	CMA_DEBUG("match_endl pos=%zu end=%zu\n", (size_t)(s->pos - s->str), (size_t)(s->end - s->str));
	return s->pos >= s->end ? cma_ret_matched(s) : cma_ret_missed(s);
}

CMA_OP cma_match_str(cma_state *s, cma_op *op) {
	cma_op_str *o = (cma_op_str *)op;
	CMA_DEBUG("match_str \"%.*s\" at pos=%zu\n", (int)o->len, o->buf, (size_t)(s->pos - s->str));

	if (s->pos + o->len > s->end) {
		// not enough input — check if available prefix matches
		size_t avail = s->end - s->pos;
		int match = 1;
		if (avail > 0) {
			if (op->flags & CMA_STR_INSENSITIVE) {
				for (size_t i = 0; i < avail; i++) {
					if (tolower(s->pos[i]) != tolower(o->buf[i])) {
						match = 0;
						break;
					}
				}
			} else {
				match = (memcmp(s->pos, o->buf, avail) == 0);
			}
		}
		if (match)
			return cma_ret_end_reached(s); // could match with more data
		return cma_ret_missed(s); // prefix already wrong, definite miss
	}

	int match;
	if (op->flags & CMA_STR_INSENSITIVE) {
		match = 1;
		for (size_t i = 0; i < o->len; i++) {
			if (tolower(s->pos[i]) != tolower(o->buf[i])) {
				match = 0;
				break;
			}
		}
	} else {
		// u32 fast reject before memcmp
		if (o->len >= 4 && *(uint32_t *)s->pos != *(uint32_t *)o->buf)
			return cma_ret_missed(s);
		
		match = (memcmp(s->pos, o->buf, o->len) == 0);
	}

	if (!match)
		return cma_ret_missed(s);

	s->pos += o->len;
	return cma_ret_matched(s);
}

CMA_OP cma_match_set(cma_state *s, cma_op *op) {
	cma_op_set *o = (cma_op_set *)op;
	CMA_DEBUG("match_set at pos=%zu ch=%c\n", (size_t)(s->pos - s->str), s->pos < s->end ? *s->pos : '?');

	if (s->pos >= s->end) return cma_ret_end_reached(s);

	uint8_t ch = *s->pos;
	if (!CMA_BIT_TEST(o->bits, ch))
		return cma_ret_missed(s);

	s->pos++;
	return cma_ret_matched(s);
}

CMA_OP cma_match_and(cma_state *s, cma_op *op) {
	cma_op_seq *o = (cma_op_seq *)op;
	CMA_DEBUG("match_and (%zu) at pos=%zu\n", o->seq_len, (size_t)(s->pos - s->str));
	uint32_t sp = cma_push(s);

	cma_op **p = o->seq;
	while (*p) {
		if (!cma_call(s, *p)) {
			CMA_DEBUG("match_and miss\n");
			cma_backtrack(s, sp);
			return cma_ret_missed(s);
		}
		p++;
	}

	cma_commit(s, sp);
	CMA_DEBUG("match_and matched\n");
	return cma_ret_matched(s);
}

CMA_OP cma_match_or(cma_state *s, cma_op *op) {
	cma_op_seq *o = (cma_op_seq *)op;
	CMA_DEBUG("match_or (%zu) at pos=%zu\n", o->seq_len, (size_t)(s->pos - s->str));

	// each alternative backtracks itself on failure, no push needed here
	cma_op **p = o->seq;
	while (*p) {
		if (cma_call(s, *p)) {
			CMA_DEBUG("match_or hit\n");
			return cma_ret_matched(s);
		}
		p++;
	}

	CMA_DEBUG("match_or miss\n");
	return cma_ret_missed(s);
}

CMA_OP cma_match_not(cma_state *s, cma_op *op) {
	cma_op_wrap *o = (cma_op_wrap *)op;
	CMA_DEBUG("match_not at pos=%zu\n", (size_t)(s->pos - s->str));

	uint32_t sp = cma_push(s);

	int ok = cma_call(s, o->pattern);

	cma_backtrack(s, sp);

	CMA_DEBUG("match_not %s\n", ok ? "miss" : "matched");
	return ok ? cma_ret_missed(s) : cma_ret_matched(s);
}

CMA_OP cma_match_ahead(cma_state *s, cma_op *op) {
	cma_op_wrap *o = (cma_op_wrap *)op;
	CMA_DEBUG("match_ahead at pos=%zu\n", (size_t)(s->pos - s->str));

	uint32_t sp = cma_push(s);

	int ok = cma_call(s, o->pattern);

	cma_backtrack(s, sp);

	CMA_DEBUG("match_ahead %s\n", ok ? "matched" : "miss");
	return ok ? cma_ret_matched(s) : cma_ret_missed(s);
}

CMA_OP cma_match_ref(cma_state *s, cma_op *op) {
	cma_op_ref *o = (cma_op_ref *)op;
	CMA_DEBUG("match_ref at pos=%zu\n", (size_t)(s->pos - s->str));
	return cma_call(s, o->cb(s, o->ctx));
}

CMA_OP cma_match_cap(cma_state *s, cma_op *op) {
	cma_op_cap *o = (cma_op_cap *)op;
	CMA_DEBUG("match_cap \"%s\" at pos=%zu\n", o->name ? o->name : "(anon)", (size_t)(s->pos - s->str));

	uint32_t sp = cma_push(s);

	uint32_t cap_idx = cma_cap_push(s);
	uint32_t inner_start = s->cap_len;
	const uint8_t *start = s->pos;

	if (!cma_call(s, o->pattern)) {
		CMA_DEBUG("match_cap miss\n");
		cma_backtrack(s, sp);
		return cma_ret_missed(s);
	}

	cma_capture *cap = &s->caps[cap_idx];
	cap->parent = op;
	cap->start  = start;
	cap->end    = s->pos;
	cap->nested = s->cap_len - inner_start;

	if ((op->flags & CMA_CAP_CB) && !o->cb(s, op->opctx, op)) {
		CMA_DEBUG("match_cap cb rejected\n");
		cma_backtrack(s, sp);
		return cma_ret_missed(s);
	}

	cma_commit(s, sp);

	CMA_DEBUG("match_cap matched \"%.*s\" nested=%u\n",
		(int)(cap->end - cap->start), cap->start, cap->nested);
	return cma_ret_matched(s);
}

CMA_OP cma_match_rep(cma_state *s, cma_op *op) {
	cma_op_rep *o = (cma_op_rep *)op;
	CMA_DEBUG("match_rep {%u,%u} at pos=%zu\n", o->min, o->max, (size_t)(s->pos - s->str));

	uint32_t sp = cma_push(s);
	uint32_t count = 0;

	while (count < o->max) {
		const uint8_t *before = s->pos;

		if (!cma_call(s, o->pattern) || s->pos == before) break;

		count++;
	}

	if (count < o->min) {
		CMA_DEBUG("match_rep miss count=%u < min=%u\n", count, o->min);
		cma_backtrack(s, sp);
		return cma_ret_missed(s);
	}

	cma_commit(s, sp);
	CMA_DEBUG("match_rep matched count=%u\n", count);
	return cma_ret_matched(s);
}

// tight scalar loop for REP of SET — no per-char cma_call dispatch
CMA_OP cma_match_rep_set_nosimd(cma_state * restrict s, cma_op * restrict op) {
	cma_op_rep *o = (cma_op_rep *)op;
	cma_op_set *inner = (cma_op_set *)o->pattern;
	uint32_t sp = cma_push(s);

	const uint8_t *pos = s->pos;
	const uint8_t *end = s->end;
	uint32_t count = 0;

	while (count < o->max && pos < end) {
		if (!CMA_BIT_TEST(inner->bits, *pos)) break;

		pos++;
		count++;
	}

	// scan truncated by end of input — stream might have more
	if (pos >= end && count < o->max) {
		if (!(s->flags & CMA_END_IS_MISS))
			return cma_ret_end_reached(s);
		// one-shot mode: fall through to normal min check
	}

	if (count < o->min) {
		cma_backtrack(s, sp);
		return cma_ret_missed(s);
	}

	s->pos = pos;
	cma_commit(s, sp);
	return cma_ret_matched(s);
}

#ifdef CMA_USE_SIMD
// SSSE3 version — 16 bytes at a time using pshufb charset lookup
//
// The 256-bit charset is reorganized into two 16-byte column LUTs
// (precomputed in cma_set_build_simd):
//   simd_lo[col] = bitmask of rows 0-7 that contain char (row*16+col)
//   simd_hi[col] = bitmask of rows 8-15 that contain char ((row+8)*16+col)
// A third LUT maps high nibble to bit mask: 1 << (hi_nib & 7)
//
// For each input byte: col lookup via pshufb(lut, lo_nibble),
// row bit via pshufb(bit_lut, hi_nibble), blend lo/hi by cmpgt,
// AND → nonzero means char is in set.
CMA_OP cma_match_rep_set(cma_state * restrict s, cma_op * restrict op) {
	cma_op_rep *o = (cma_op_rep *)op;
	cma_op_set *inner = (cma_op_set *)o->pattern;
	uint32_t sp = cma_push(s);

	const uint8_t *pos = s->pos;
	const uint8_t *end = s->end;
	uint32_t count = 0;
	uint32_t max = o->max;

	__m128i lut_lo = _mm_loadu_si128((const __m128i *)inner->simd_lo);
	__m128i lut_hi = _mm_loadu_si128((const __m128i *)inner->simd_hi);

	static const uint8_t _bl[16] __attribute__((aligned(16))) =
		{1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128};
	__m128i bit_lut  = _mm_load_si128((const __m128i *)_bl);
	__m128i nib_mask = _mm_set1_epi8(0x0F);
	__m128i seven    = _mm_set1_epi8(7);
	__m128i zero     = _mm_setzero_si128();

	while (count + 16 <= max && pos + 16 <= end) {
		__m128i input = _mm_loadu_si128((const __m128i *)pos);

		__m128i lo_nib = _mm_and_si128(input, nib_mask);
		__m128i hi_nib = _mm_and_si128(_mm_srli_epi16(input, 4), nib_mask);

		// Column bits for rows 0-7 and 8-15
		__m128i col_lo = _mm_shuffle_epi8(lut_lo, lo_nib);
		__m128i col_hi = _mm_shuffle_epi8(lut_hi, lo_nib);

		// Bit mask for this row: 1 << (hi_nib & 7)
		__m128i row_bit = _mm_shuffle_epi8(bit_lut, hi_nib);

		// Blend: col_hi for hi_nib > 7, col_lo otherwise
		__m128i upper = _mm_cmpgt_epi8(hi_nib, seven);
		__m128i col = _mm_or_si128(
			_mm_and_si128(upper, col_hi),
			_mm_andnot_si128(upper, col_lo));

		__m128i match = _mm_and_si128(col, row_bit);

		// Each byte nonzero = char in set; find first zero
		int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(match, zero));
		if (mask != 0) {
			int n = __builtin_ctz(mask);
			count += n;
			pos += n;
			goto done;
		}
		pos += 16;
		count += 16;
	}

	// Scalar tail
	while (count < max && pos < end) {
		if (!CMA_BIT_TEST(inner->bits, *pos)) break;
		pos++; count++;
	}

done:
	if (pos >= end && count < max) {
		if (!(s->flags & CMA_END_IS_MISS))
			return cma_ret_end_reached(s);
	}

	if (count < o->min) {
		cma_backtrack(s, sp);
		return cma_ret_missed(s);
	}

	s->pos = pos;
	cma_commit(s, sp);
	return cma_ret_matched(s);
}
#else
// No SSSE3 — fall back to scalar
CMA_OP cma_match_rep_set(cma_state * restrict s, cma_op * restrict op) {
	return cma_match_rep_set_nosimd(s, op);
}
#endif

// ================================================================
//  init — initialize opcode nodes in pre-allocated storage
// ================================================================

cma_op *cma_init_str(cma_op_str *o, const char *str, uint16_t flags) {
	o->base.fn    = cma_match_str;
	o->base.type  = CMA_STR;
	o->base.flags = flags;
	o->buf        = (const uint8_t *)str;
	o->len        = strlen(str);
	return (cma_op *)o;
}

// ================================================================
//  character set initialization helpers
// ================================================================

void cma_set_build_simd(cma_op_set *o);

void cma_set_fill_str(cma_op_set *o, const char *spec) {
	const uint8_t *p = (const uint8_t *)spec;
	while (*p) {
		if (p[1] == '-' && p[2]) {
			uint8_t lo = p[0], hi = p[2];
			if (lo > hi) {
				uint8_t t = lo;
				lo = hi;
				hi = t;
			}
			for (unsigned c = lo; c <= hi; c++) {
				CMA_BIT_SET(o->bits, c);
			}
			p += 3;
		} else {
			CMA_BIT_SET(o->bits, *p);
			p++;
		}
	}
	cma_set_build_simd(o);
}

void cma_set_fill_range(cma_op_set *o, uint8_t start, uint8_t end) {
	if (start > end) {
		uint8_t t = start;
		start = end;
		end = t;
	}
	for (unsigned c = start; c <= end; c++) {
		CMA_BIT_SET(o->bits, c);
	}
	cma_set_build_simd(o);
}

void cma_set_fill_chrs(cma_op_set *o, const uint8_t *chars, size_t count) {
	for (size_t i = 0; i < count; i++) {
		CMA_BIT_SET(o->bits, chars[i]);
	}
	cma_set_build_simd(o);
}

#ifdef CMA_USE_SIMD
void cma_set_build_simd(cma_op_set *o) {
	for (int col = 0; col < 16; col++) {
		uint8_t b1 = 0, b2 = 0;
		for (int row = 0; row < 8; row++) {
			if (CMA_BIT_TEST(o->bits, row * 16 + col))
				b1 |= (1 << row);
			if (CMA_BIT_TEST(o->bits, (row + 8) * 16 + col))
				b2 |= (1 << row);
		}
		o->simd_lo[col] = b1;
		o->simd_hi[col] = b2;
	}
}
#else
void cma_set_build_simd(cma_op_set *o) { (void)o; }
#endif

void cma_set_invert(cma_op_set *o) {
	for (size_t i = 0; i < CMA_BITSET_WORDS; i++) {
		o->bits[i] = ~o->bits[i];
	}
	cma_set_build_simd(o);
}

cma_op *cma_init_set(cma_op_set *o) {
	o->base.fn    = cma_match_set;
	o->base.type  = CMA_SET;
	o->base.flags = 0;
	memset(o->bits, 0, sizeof(o->bits));
	return (cma_op *)o;
}


// n==1: return the single op directly instead of wrapping
cma_op *cma_init_seq(cma_op_seq *o, uint16_t type, const char *name, size_t n, cma_op **ops) {
	if (n == 1 && !name) return ops[0];

	o->base.fn    = (type == CMA_OR) ? cma_match_or : cma_match_and;
	o->base.type  = type;
	o->base.flags = 0;
	o->name       = (char *)name;
	o->seq_len    = n;
	memcpy(&o->seq, ops, n * sizeof(cma_op *));
	o->seq[n] = NULL;

	return (cma_op *)o;
}

cma_op *cma_init_not(cma_op_wrap *o, cma_op *pattern) {
	o->base.fn    = cma_match_not;
	o->base.type  = CMA_NOT;
	o->base.flags = 0;
	o->pattern    = pattern;
	return (cma_op *)o;
}

cma_op *cma_init_ahead(cma_op_wrap *o, cma_op *pattern) {
	o->base.fn    = cma_match_ahead;
	o->base.type  = CMA_AHEAD;
	o->base.flags = 0;
	o->pattern    = pattern;
	return (cma_op *)o;
}

cma_op *cma_init_ref(cma_op_ref *o, cma_ref_fn cb, void *ctx) {
	o->base.fn    = cma_match_ref;
	o->base.type  = CMA_REF;
	o->base.flags = 0;
	o->cb         = cb;
	o->ctx        = ctx;
	return (cma_op *)o;
}

// select fast path at init time based on inner pattern type
cma_op *cma_init_rep(cma_op_rep *o, uint32_t min, uint32_t max, cma_op *pattern) {
	o->base.type  = CMA_REP;
	o->base.flags = 0;
	o->min        = min;
	o->max        = max;
	o->pattern    = pattern;

	if (pattern->type == CMA_SET) {
		o->base.fn = cma_match_rep_set;
	} else {
		o->base.fn = cma_match_rep;
	}

	return (cma_op *)o;
}

cma_op *cma_init_cap(cma_op_cap *o, cma_op *pattern, uint16_t flags, void* name) {
	o->base.fn    = cma_match_cap;
	o->base.type  = CMA_CAP;
	o->base.flags = flags;
	o->pattern    = pattern;
	o->name       = name;
	return (cma_op *)o;
}

cma_op *cma_init_cap_cb(cma_op_cap *o, cma_op *pattern, cma_cap_cb_fn cb, void *ctx) {
	o->base.fn    = cma_match_cap;
	o->base.type  = CMA_CAP;
	o->base.flags = CMA_CAP_CB;
	o->base.opctx = ctx;
	o->pattern    = pattern;
	o->cb         = cb;
	return (cma_op *)o;
}

// ================================================================
//  top-level runner
// ================================================================

void cma_init(cma_state *s, const char *input, size_t len) {
	memset(s, 0, sizeof(*s));
	s->str    = (const uint8_t *)input;
	s->length = len;
	s->pos      = s->str;
	s->end      = s->str + len;
	s->farthest = s->str;
	s->flags    = CMA_END_IS_MISS;
}

int cma_match(cma_state *s, cma_op *op) {
	if (_setjmp(s->jumpout)) {
		return -1;
	}

	int matched = cma_call(s, op);
	return matched ? (int)(s->pos - s->str) : -1;
}

void cma_free(cma_state *s) {
	free(s->stack);
	free(s->caps);
	s->stack = NULL;
	s->caps  = NULL;
}

int cma_run(cma_op *op, const char *input, size_t len) {
	cma_state s = {0};
	cma_init(&s, input, len);
	return cma_match(&s, op);
}

// ================================================================
//  debug dump
// ================================================================

static const char *type_name(uint16_t t) {
	switch (t) {
		case CMA_STR:   return "STR";
		case CMA_SET:   return "SET";
		case CMA_AND:   return "AND";
		case CMA_OR:    return "OR";
		case CMA_NOT:   return "NOT";
		case CMA_REP:   return "REP";
		case CMA_AHEAD: return "AHEAD";
		case CMA_REF:   return "REF";
		case CMA_ENDL:  return "ENDL";
		case CMA_CAP:   return "CAP";
		case CMA_NOOP:  return "NOOP";
		case CMA_EXEC:  return "EXEC";
		default:        return "???";
	}
}

static void pad(int n) {
	for (int i = 0; i < n; i++) printf("  ");
}

void cma_dump(cma_op *op, int indent) {
	pad(indent);
	printf("[%s]", type_name(op->type));

	switch (op->type) {
	case CMA_STR: {
		cma_op_str *o = (cma_op_str *)op;
		printf(" \"%.*s\"\n", (int)o->len, o->buf);
		break;
	}
	case CMA_SET: {
		cma_op_set *o = (cma_op_set *)op;
		printf(" {");
		for (int c = 0; c < 256; c++) {
			if (CMA_BIT_TEST(o->bits, c)) {
				if (isprint(c)) putchar(c);
				else printf("\\x%02x", c);
			}
		}
		printf("}\n");
		break;
	}
	case CMA_AND:
	case CMA_OR: {
		cma_op_seq *o = (cma_op_seq *)op;
		if (o->name) printf(" \"%s\"", o->name);
		printf(" (%zu)\n", o->seq_len);
		for (size_t i = 0; i < o->seq_len; i++) {
			cma_dump(o->seq[i], indent + 1);
		}
		break;
	}
	case CMA_NOT:
	case CMA_AHEAD: {
		cma_op_wrap *o = (cma_op_wrap *)op;
		printf("\n");
		cma_dump(o->pattern, indent + 1);
		break;
	}
	case CMA_ENDL:
	case CMA_NOOP:
		printf("\n");
		break;
	case CMA_EXEC:
		printf(" fn=%p ctx=%p\n", (void *)(uintptr_t)op->fn, op->opctx);
		break;
	case CMA_REF: {
		cma_op_ref *o = (cma_op_ref *)op;
		cma_op *target = o->cb(NULL, o->ctx);
		printf(" -> %p\n", (void *)target);
		break;
	}
	case CMA_REP: {
		cma_op_rep *o = (cma_op_rep *)op;
		if (o->max == CMA_INF) {
			printf(" {%u,inf}\n", o->min);
		} else {
			printf(" {%u,%u}\n", o->min, o->max);
		}
		cma_dump(o->pattern, indent + 1);
		break;
	}
	case CMA_CAP: {
		cma_op_cap *o = (cma_op_cap *)op;
		printf(" \"%s\"\n", o->name ? o->name : "(anon)");
		cma_dump(o->pattern, indent + 1);
		break;
	}
	}
}

void cma_dump_captures(cma_state *s) {
	printf("captures: %u\n", s->cap_len);
	for (uint32_t i = 0; i < s->cap_len; i++) {
		cma_capture *c = &s->caps[i];
		cma_op_cap *op = (cma_op_cap *)c->parent;
		printf("  [%u] \"%s\" = \"%.*s\" (nested=%u)\n",
			i,
			op->name ? op->name : "(anon)",
			(int)(c->end - c->start), c->start,
			c->nested);
	}
}
