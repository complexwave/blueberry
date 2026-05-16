/*
 * uscale_min.c -- minimal uscale subset for ci_number.
 * Only uscale_dtoa_short: double → shortest (d, p) pair.
 *
 * Extracted from uscale.c (BSD licensed, Russ Cox / Go Authors).
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "uscale_pow10.h"


/* ============================================================
 * Types
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
 * Unrounded value
 * ============================================================ */

typedef uint64_t uscale_unrounded;

static inline uint64_t uscale_ufloor(uscale_unrounded u) { return (u + 0) >> 2; }
static inline uint64_t uscale_uceil(uscale_unrounded u)  { return (u + 3) >> 2; }
static inline uscale_unrounded uscale_unudge(uscale_unrounded u, int d) { return u + d; }
static inline uint64_t uscale_uround(uscale_unrounded u) { return (u + 1 + ((u >> 2) & 1)) >> 2; }

static inline uscale_unrounded uscale_udiv(uscale_unrounded u, uint64_t d) {
	return (u / d) | (u & 1) | (u % d != 0);
}

/* ============================================================
 * Prescale / scale core
 * ============================================================ */

static inline uscale_scaler uscale_prescale(int e, int p, int lp) {
	if(p < pow10Min || p > pow10Max) {
		fprintf(stderr, "uscale: pow10 range exceeded\n");
		return (uscale_scaler){0, 0, 0};
	}
	int s = -(e + lp + 3);
	uscale_scaler pre;
	p -= pow10Min;
	pre.pmHi = pow10Tab[p][0];
	pre.pmLo = pow10Tab[p][1];
	pre.s = s;
	return pre;
}

static uscale_unrounded uscale_scale(uint64_t x, uscale_scaler c) {
	unsigned __int128 full = (unsigned __int128)x * c.pmHi;
	uint64_t hi = full >> 64;
	uint64_t mid1 = full;
	uint64_t sticky = 1;
	if ((hi & ((1ULL << c.s) - 1)) == 0) {
		uint64_t mid2 = ((unsigned __int128)x * c.pmLo) >> 64;
		sticky = (mid1 - mid2 > 1);
		hi -= mid1 < mid2;
	}
	return (hi >> c.s) | sticky;
}

/* ============================================================
 * Unpack IEEE 754 double
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

/* ============================================================
 * Pack IEEE 754 double
 * ============================================================ */

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

	if ((d = uscale_rotateRight64(x * div1e1m, 1)) <= div1e1le) {
		x = d;
		p++;
	} else {
		return (uscale_digits){x, p};
	}

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
 * dtoa -- shortest representation
 * ============================================================ */

static uscale_digits uscale_dtoa_short(double f) {
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
