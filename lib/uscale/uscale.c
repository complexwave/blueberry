/*
 * uscale.c -- float-to-string and string-to-float via scaled multiplication.
 *
 * Ported from fpfmt/bench/uscalec/ftoa.c (BSD licensed, see LICENSE).
 * Original algorithm by Russ Cox / Go Authors.
 *
 * Public API:
 *   uscale_dtoa_short(f)       -- shortest round-trip d * 10^p
 *   uscale_dtoa_fixed(f, n)    -- fixed n-digit d * 10^p
 *   uscale_atod(d, p)          -- decimal d * 10^p to double
 *   uscale_atod_text(s, len)   -- decimal string to double
 *   uscale_format(dst, d, p, nd) -- render d * 10^p into e-notation
 *   uscale_digits(d)           -- count decimal digits in d
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "uscale_pow10.h"

/* ============================================================
 * Result types
 * ============================================================ */

typedef struct uscale_digits uscale_digits;
struct uscale_digits {
	uint64_t d;
	int p;
};

typedef struct uscale_scaler uscale_scaler;
struct uscale_scaler {
	uint64_t pmHi;
	uint64_t pmLo;
	int s;
};

/* ============================================================
 * Bit utilities
 * ============================================================ */

static inline uint64_t uscale_rotateRight64(uint64_t x, int s) {
	return (x >> s) | (x << (64 - s));
}

static inline int uscale_bitsLen64(uint64_t x) {
	return 64 - __builtin_clzll(x);
}

/* ============================================================
 * Log approximations
 * ============================================================ */

static inline int uscale_log10Pow2(int x) {
	return (x * 78913) >> 18;
}

static inline int uscale_log2Pow10(int x) {
	return (x * 108853) >> 15;
}

static inline int uscale_skewed(int e) {
	return (e * 631305 - 261663) >> 21;
}

/* ============================================================
 * Powers of 10 (small, uint64)
 * ============================================================ */

static uint64_t uscale_uint64pow10[] = {
	1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
	1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
};

/* ============================================================
 * Unrounded value
 *
 * An unrounded stores 2 extra fractional bits plus a sticky bit,
 * enabling correct rounding without full-width arithmetic.
 * Representation: value * 4, with bit 0 = sticky.
 * ============================================================ */

static inline int uscale_isDigit(char c) {
	return (unsigned char)(c - '0') <= 9;
}

typedef uint64_t uscale_unrounded;

static inline uint64_t uscale_ufloor(uscale_unrounded u) { return (u + 0) >> 2; }
static inline uint64_t uscale_uceil(uscale_unrounded u)  { return (u + 3) >> 2; }
static inline uscale_unrounded uscale_unudge(uscale_unrounded u, int d) { return u + d; }
static inline uint64_t uscale_uround(uscale_unrounded u) { return (u + 1 + ((u >> 2) & 1)) >> 2; }

static inline uscale_unrounded uscale_udiv(uscale_unrounded u, uint64_t d) {
	return (u / d) | (u & 1) | (u % d != 0);
}

static inline uscale_unrounded uscale_ursh(uscale_unrounded u, int s) {
	return (u >> s) | (u & 1) | ((u & ((1 << s) - 1)) != 0);
}

/* ============================================================
 * Prescale / uscale core
 * ============================================================ */

static inline uscale_scaler uscale_prescale(int e, int p, int lp) {
	if(p < pow10Min || p > pow10Max)
		abort();
	int s = -(e + lp + 3);
	uscale_scaler pre;
	p -= pow10Min;
	pre.pmHi = pow10Tab[p][0];
	pre.pmLo = pow10Tab[p][1];
	pre.s = s;
	return pre;
}

static uscale_unrounded uscale_scale(uint64_t x, uscale_scaler c) {
	//printf("uscale x=%#llx c=%#llx %#llx %d\n", x, c.pmHi, c.pmLo, c.s);
	unsigned __int128 full = (unsigned __int128)x * c.pmHi;
	uint64_t hi = full >> 64;
	uint64_t mid1 = full;
	//printf("hi=%#llx mid1=%#llx\n", hi, mid1);
	uint64_t sticky = 1;
	if ((hi & ((1ULL << c.s) - 1)) == 0) {
		uint64_t mid2 = ((unsigned __int128)x * c.pmLo) >> 64;
		//printf("mid2=%#llx\n", mid2);
		sticky = (mid1 - mid2 > 1);
		hi -= mid1 < mid2;
	}
	return (hi >> c.s) | sticky;
}

/* ============================================================
 * Unpack / pack IEEE 754 double
 * ============================================================ */

static inline void uscale_unpack64(double f, uint64_t *mp, int *ep) {
	uint64_t b;
	memcpy(&b, &f, sizeof f);
	uint64_t m = (b & ((1ULL << 52) - 1)) << 11;
	int e = ((b >> 52) & ((1 << 11) - 1)) - 1086;
	if (e == -1086) {
		e = -1085;
		int s = __builtin_clzll(m);
		m <<= s;
		e -= s;
	} else {
		m |= 1ULL << 63;
	}
	*mp = m;
	*ep = e;
}

static inline double uscale_pack64(uint64_t m, int e) {
	if((m & (1ULL << 52)) != 0)
		m = (m & ~(1ULL << 52)) | ((uint64_t)(1075 + e) << 52);
	double f;
	memcpy(&f, &m, sizeof f);
	return f;
}

/* ============================================================
 * Trim trailing zeros
 * ============================================================ */

static inline uscale_digits uscale_trimZeros(uint64_t x, int p) {
	const uint64_t maxUint64 = ~0ULL;
	const uint64_t div1e8m = 0xc767074b22e90e21ULL;
	const uint64_t div1e4m = 0xd288ce703afb7e91ULL;
	const uint64_t div1e2m = 0x8f5c28f5c28f5c29ULL;
	const uint64_t div1e1m = 0xcccccccccccccccdULL;
	const uint64_t div1e8le = maxUint64 / 100000000;
	const uint64_t div1e4le = maxUint64 / 10000;
	const uint64_t div1e2le = maxUint64 / 100;
	const uint64_t div1e1le = maxUint64 / 10;

	uint64_t d;

	/* Cut 1 zero, or else return. */
	if ((d = uscale_rotateRight64(x * div1e1m, 1)) <= div1e1le) {
		x = d;
		p++;
	} else {
		return (uscale_digits){x, p};
	}

	/* Cut 8 zeros, then 4, then 2, then 1. */
	if ((d = uscale_rotateRight64(x * div1e8m, 8)) <= div1e8le) {
		x = d;
		p += 8;
	}
	if ((d = uscale_rotateRight64(x * div1e4m, 4)) <= div1e4le) {
		x = d;
		p += 4;
	}
	if ((d = uscale_rotateRight64(x * div1e2m, 2)) <= div1e2le) {
		x = d;
		p += 2;
	}
	if ((d = uscale_rotateRight64(x * div1e1m, 1)) <= div1e1le) {
		x = d;
		p += 1;
	}
	return (uscale_digits){x, p};
}

/* ============================================================
 * dtoa -- fixed-width n digits
 * ============================================================ */

uscale_digits uscale_dtoa_fixed(double f, int n) {
	uint64_t m;
	int e;
	uscale_unpack64(f, &m, &e);
	//printf("unpack %.17g %#llx %d n=%d\n", f, m, e, n);
	int p = n - 1 - uscale_log10Pow2(e + 63);
	//printf("p = %d\n", p);
	uscale_unrounded u = uscale_scale(m, uscale_prescale(e, p, uscale_log2Pow10(p)));
	//printf("u = %#llx\n", u);
	uint64_t d = uscale_uround(u);
	//printf("d = %lld\n", d);
	if(d >= uscale_uint64pow10[n]) {
		d = uscale_uround(uscale_udiv(u, 10));
		p--;
	}
	return (uscale_digits){d, -p};
}

/* ============================================================
 * dtoa -- shortest representation
 * ============================================================ */

uscale_digits uscale_dtoa_short(double f) {
	uint64_t m;
	int e;
	uscale_unpack64(f, &m, &e);

	int p;
	uint64_t min;
	int b = 11;
	if (m == 1ULL << 63 && e > -1085) {
		p = -uscale_skewed(e + b);
		min = m - (1ULL << (b - 2));
	} else {
		if (e < -1085) {
			b = 11 + (-1085 - e);
		}
		p = -uscale_log10Pow2(e + b);
		min = m - (1ULL << (b - 1));
	}
	uint64_t max = m + (1ULL << (b - 1));

	int odd = (m >> b) & 1;
	uscale_scaler pre = uscale_prescale(e, p, uscale_log2Pow10(p));
	uint64_t dmin = uscale_uceil(uscale_unudge(uscale_scale(min, pre), +odd));
	uint64_t dmax = uscale_ufloor(uscale_unudge(uscale_scale(max, pre), -odd));

	//printf("cshort f=%.17g m=%#llx e=%d p=%d b=%d odd=%d\nmin=%#llx max=%#llx dmin=%lld dmax=%lld d0=%lld\n",
	//	f, m, e, p, b, odd, min, max, dmin, dmax, d0);

	uint64_t d0 = dmax / 10 * 10;
	if (d0 >= dmin) {
		return uscale_trimZeros(dmax / 10, -(p - 1));
	}
	uint64_t d = dmin;
	if (d < dmax)
		d = uscale_uround(uscale_scale(m, pre));
	return (uscale_digits){d, -p};
}

/* ============================================================
 * atod -- decimal to double
 * ============================================================ */

double uscale_atod(uint64_t d, int p) {
	int b = uscale_bitsLen64(d);
	int lp = uscale_log2Pow10(p);
	int e = 53 - b - lp;
	if (e > 1074)
		e = 1074;
	uscale_unrounded u = uscale_scale(d << (64 - b), uscale_prescale(e - (64 - b), p, lp));
	int adj = (u >= (1ULL << 55) - 2);
	u = (u >> adj) | (u & 1);
	e -= adj;
	uint64_t m = uscale_uround(u);
	return uscale_pack64(m, -e);
}

/* ============================================================
 * atod -- parse decimal text to double
 * ============================================================ */

double uscale_atod_text(const char *s, int len) {
	const int maxDigits = 19;
	uint64_t d = 0;
	int frac = 0;
	int i = 0;

	/* Read digits. */
	for(; i < len && uscale_isDigit(s[i]); i++)
		d = d * 10 + s[i] - '0';
	if(i > maxDigits)
		return 0;
	if(i < len && s[i] == '.') {
		i++;
		for(; i < len && uscale_isDigit(s[i]); i++) {
			d = d * 10 + s[i] - '0';
			frac++;
		}
		if(i == 1 || i > maxDigits + 1)
			return 0;
	}
	if(i == 0)
		return 0;

	/* Read exponent. */
	int p = 0;
	if(i < len && (s[i] == 'e' || s[i] == 'E')) {
		i++;
		int sign = +1;
		if(i < len) {
			if(s[i] == '-') {
				sign = -1;
				i++;
			} else if(s[i] == '+') {
				i++;
			}
		}
		if(i >= len || len - i > 3)
			return 0;
		for(; i < len && uscale_isDigit(s[i]); i++)
			p = p * 10 + s[i] - '0';
		p *= sign;
	}
	if(i != len)
		return 0;
	return uscale_atod(d, p - frac);
}

/* ============================================================
 * Digit count
 * ============================================================ */

static inline int uscale_digits_count(uint64_t d) {
	int nd = uscale_log10Pow2(uscale_bitsLen64(d));
	return nd + (d >= uscale_uint64pow10[nd]);
}

/* ============================================================
 * Formatting -- render digits into e-notation
 * ============================================================ */

static void uscale_formatBase10(char *dst, uint64_t d64, int nd) {
	for (int i = nd - 1; i >= 0; i--) {
		dst[i] = '0' + (d64 % 10);
		d64 /= 10;
	}
}

/* printdecnumber: write digits of v backwards into tmp, return count */
static inline int uscale_printdecnumber(char *tmp, uint64_t v) {
	int n = 0;
	do {
		tmp[n++] = '0' + (v % 10);
		v /= 10;
	} while (v);
	/* reverse in place */
	for (int i = 0, j = n - 1; i < j; i++, j--) {
		char c = tmp[i]; tmp[i] = tmp[j]; tmp[j] = c;
	}
	return n;
}

/*
 * uscale_dtoa_pretty -- format double for scripting language display.
 *
 * max_ints: max integer digits before switching to scientific
 * fdmax:    max fractional digits to print (0 = all, <0 = threshold for scientific)
 *
 * Returns number of bytes written (not counting NUL).
 */
#define DTOA_COPY_DIGITS while (src < end) *out++ = *src++;
#define DTOA_CHAR(ch) *out++ = ch;

int uscale_dtoa_pretty(char *dst, double f, int max_ints, int fdmax) {
	char *out = dst;

	/* handle sign */
	if (f < 0) {
		*out++ = '-';
		f = -f;
	}

	/* TODO: handle 0, inf, nan */

	uscale_digits r = uscale_dtoa_short(f);

	char digits[32];
	int int_digits = uscale_printdecnumber(digits, r.d);
	
	int p = r.p;
	char *src = digits;
	char *end = src + int_digits;

	if (p >= 0) {
		/* expressable without dot */
		int trailing_zeros = p;
		int int_print_length = int_digits + p;

		if (int_print_length > max_ints) {
			if (p) {
				/* print d then e<p> -- but need normalized form */
				goto print_normalized;
			}
		} else {
			DTOA_COPY_DIGITS;
			
			while(trailing_zeros--)
				DTOA_CHAR('0');
			
			goto finalize;
		}
	} 
	
	

	int float_total_print_digits = -p;
	int int_print_length = int_digits - float_total_print_digits;
	if (int_print_length < 0) int_print_length = 0;

	int float_significant_digits = int_digits - int_print_length;

	int leading_zeroes = float_total_print_digits - float_significant_digits;
	
	if (fdmax < 0) {
		fdmax = -fdmax;

		if( float_total_print_digits >= fdmax){
			goto print_normalized;
		}
	}
	
	if(!fdmax && leading_zeroes){
		goto print_normalized;
	}
	
	if (int_print_length > 0) {
		if (int_print_length > max_ints)
			goto print_normalized;

		while (int_print_length--)
			*out++ = *src++;

		DTOA_CHAR('.');
	} else {
		DTOA_CHAR('0');
		DTOA_CHAR('.');
	}
	
	if (fdmax) {
		
		while(leading_zeroes-- && fdmax){
				DTOA_CHAR('0');
				fdmax--;
		}
		
		while (fdmax-- && (src < end))
			*out++ = *src++;
		
		goto finalize;
	}
	
	
	DTOA_COPY_DIGITS;
	goto finalize;

	print_normalized:
		p = int_digits - 1 + r.p;
		
		src = digits;
		*out++ = *src++;
		
		if(src < end){
			*out++ = '.';

			DTOA_COPY_DIGITS;
		}
		
	print_exponent:;
		/* print exponent */
		if(p){
			*out++ = 'e';
			
			if (p < 0) {
				*out++ = '-';
				p = -p;
			}
			
			int exponent_length = uscale_printdecnumber(digits, p);
			char *src = digits;
			char *end = src + exponent_length;
			
			DTOA_COPY_DIGITS;
		}

	finalize:
	*out = 0;
	return out - dst;
}

void uscale_format(char *dst, uint64_t d, int p, int nd) {
	uscale_formatBase10(dst + 1, d, nd);
	p += nd - 1;
	dst[0] = dst[1];
	int n = nd;
	if(n > 1) {
		dst[1] = '.';
		n++;
	}
	dst[n] = 'e';
	if(p < 0) {
		dst[n + 1] = '-';
		p = -p;
	} else {
		dst[n + 1] = '+';
	}
	if (p < 100) {
		dst[n + 2] = '0' + p / 10;
		dst[n + 3] = '0' + p % 10;
		dst[n + 4] = 0;
		return;
	}
	dst[n + 2] = '0' + p / 100;
	dst[n + 3] = '0' + (p / 10) % 10;
	dst[n + 4] = '0' + p % 10;
	dst[n + 5] = 0;
}
