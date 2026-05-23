/*
 * ci_string_utf8.c — UTF-8 encoding helper
 *
 * Include-style — do not compile separately.
 */

#include <stdint.h>
#include <stddef.h>

#define CI_UTF8_MAX_CHAR_BYTES 4

/*
 * ci_utf8_char — encode a Unicode code point as UTF-8.
 * Writes 1–4 bytes into dst. Returns number of bytes written.
 * Invalid code points (>0x10FFFF or surrogates) → U+FFFD replacement char.
 */
static inline size_t ci_utf8_char(uint8_t *dst, uint32_t ch) {
	if (ch <= 0x7F) {
		dst[0] = (uint8_t)ch;
		return 1;
	}

	if (ch <= 0x7FF) {
		dst[0] = 0xC0 | (uint8_t)(ch >> 6);
		dst[1] = 0x80 | (uint8_t)(ch & 0x3F);
		return 2;
	}

	if (ch <= 0xFFFF) {
		/* reject surrogates */
		if (ch >= 0xD800 && ch <= 0xDFFF) goto replacement;

		dst[0] = 0xE0 | (uint8_t)(ch >> 12);
		dst[1] = 0x80 | (uint8_t)((ch >> 6) & 0x3F);
		dst[2] = 0x80 | (uint8_t)(ch & 0x3F);
		return 3;
	}

	if (ch <= 0x10FFFF) {
		dst[0] = 0xF0 | (uint8_t)(ch >> 18);
		dst[1] = 0x80 | (uint8_t)((ch >> 12) & 0x3F);
		dst[2] = 0x80 | (uint8_t)((ch >> 6) & 0x3F);
		dst[3] = 0x80 | (uint8_t)(ch & 0x3F);
		return 4;
	}

replacement:
	/* U+FFFD: EF BF BD */
	dst[0] = 0xEF;
	dst[1] = 0xBF;
	dst[2] = 0xBD;
	return 3;
}
