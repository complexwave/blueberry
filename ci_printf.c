/*
 * ci_printf.c — Citrin format string to buffer
 *
 * Single tight loop, musl-style. Parses format string, copies literals,
 * handles escapes (%%, %]), styles (%[s:..] and %:s:spec), and format
 * specs (%[0][width][.precision]specifier.
 *
 * Specifiers:  d x X o b f e E g G s c ?
 * Flags:       0
 * Escapes:     %% → %   %] → ]
 *
 * Full style:  %[red,bold: content %d here ]
 * Short style: %:red:10d
 *
 * Include-style — do not compile separately.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ============================================================
 * Style stubs — will call into scripting lang later
 * ============================================================ */

/* green text: \033[32m ... \033[0m */
#define CI_STYLE_OPEN  "\033[32m"
#define CI_STYLE_CLOSE "\033[0m"
#define CI_STYLE_OPEN_LEN  (sizeof(CI_STYLE_OPEN) - 1)
#define CI_STYLE_CLOSE_LEN (sizeof(CI_STYLE_CLOSE) - 1)

static int ci_fmt_style_open(uint8_t *out, const uint8_t *name, size_t len) {
	(void)name; (void)len;
	memcpy(out, CI_STYLE_OPEN, CI_STYLE_OPEN_LEN);
	return (int)CI_STYLE_OPEN_LEN;
}

static int ci_fmt_style_close(uint8_t *out, ci_ptr ctx) {
	(void)ctx;
	memcpy(out, CI_STYLE_CLOSE, CI_STYLE_CLOSE_LEN);
	return (int)CI_STYLE_CLOSE_LEN;
}

/* ============================================================
 * ci_printf_estimate — conservative output size estimate.
 * Used for pre-allocation. Safe to over-estimate.
 * ============================================================ */

static size_t ci_printf_estimate(const uint8_t *fmt, size_t fmtlen,
                                 ci_ptr *args, size_t arg_cnt)
{
	size_t est = fmtlen;

	for (size_t i = 0; i < arg_cnt; i++) {
		if (CI_IS_ANY_STR(args[i]))
			est += ci_str_len(args[i]);
		else if (CI_IS_ANY_NUMBER(args[i]))
			est += 10;
		else
			est += 20;
	}

	return est;
}

/* ============================================================
 * ci_printf — parse format string, write into ci_str dst
 *
 * Returns bytes written.
 * ============================================================ */

static int ci_printf(ci_ptr dst,
                     const uint8_t *fmt, size_t fmtlen,
                     ci_ptr *args, size_t arg_cnt)
{
	ci_str *s = (ci_str *)dst;

	size_t max_size = ci_printf_estimate(fmt, fmtlen, args, arg_cnt);

	if (!ci_str_ensure_tail(s, max_size))
		return 0;

	uint8_t *out_start = ci_str_tail(s);
	uint8_t *out = out_start;
	size_t bytes_avail = ci_str_tail_space(s);
	size_t total_written = 0;

	#define BYTES_ADDED    ((size_t)(out - out_start))
	#define ENSURE_SPACE(n) do { \
		if (BYTES_ADDED + (n) >= bytes_avail) { \
			size_t _ba = BYTES_ADDED; \
			ci_str_put_tail(s, _ba); \
			total_written += _ba; \
			if (!ci_str_ensure_tail(s, (n))) { \
				return (int)total_written; \
			} \
			out_start = ci_str_tail(s); \
			out = out_start; \
			bytes_avail = ci_str_tail_space(s); \
		} \
	} while(0)

	const uint8_t *cur = fmt;
	const uint8_t *end = fmt + fmtlen;
	size_t argi = 0;

	ci_ptr in_style = NULL;

	#define HAS_DATA  (cur < end)
	#define PEEK      (*cur)
	#define NEXT      (*cur++)
	#define PUT(c)    do { *out++ = (uint8_t)(c); } while(0)
	#define CONSUME_INT(p) \
			while (HAS_DATA && PEEK >= '0' && PEEK <= '9') { \
				p = p * 10 + (NEXT - '0'); }
	#define APPEND(str) do { \
			ENSURE_SPACE(sizeof(str) - 1); \
			memcpy(out, str, sizeof(str) - 1); \
			out += sizeof(str) - 1; \
	} while(0)
	#define PADD_COPY(src_buf, src_n, pc) do { \
			int _pad = (w > (src_n)) ? w - (src_n) : 0; \
			ENSURE_SPACE((src_n) + _pad); \
			if (!left_align) while (_pad > 0) { PUT(pc); _pad--; } \
			memcpy(out, (src_buf), (src_n)); \
			out += (src_n); \
			if (left_align) while (_pad > 0) { PUT(' '); _pad--; } \
	} while(0)

	/* style variables — shared by %[style: and %:style:spec paths */
	const uint8_t *sty = NULL;
	size_t sty_len = 0;
	int style_autoclose = 0;

	while (HAS_DATA) {
		/* ---- literal ---- */
		if (PEEK != '%') {
			if (in_style && PEEK == ']') {
				ENSURE_SPACE(CI_STYLE_CLOSE_LEN);
				out += ci_fmt_style_close(out, in_style);
				in_style = NULL;
				cur++;
				continue;
			}

			ENSURE_SPACE(1);
			PUT(NEXT);
			continue;
		}

		cur++; /* consume % */
		if (!HAS_DATA) { ENSURE_SPACE(1); PUT('%'); break; }

		uint8_t c = PEEK;

		/* %% */
		if (c == '%') { NEXT; ENSURE_SPACE(1); PUT('%'); continue; }

		/* %] */
		if (c == ']') { NEXT; ENSURE_SPACE(1); PUT(']'); continue; }

		/* %:style:spec — short styled, fall through to spec parse */
		if (c == ':') {
			style_autoclose = 1;
			goto process_style;
		}

		/* %[style: ... ] — block style */
		if (c == '[') {
			style_autoclose = 0;

			process_style:;

			NEXT;

			if (in_style) {
				ENSURE_SPACE(CI_STYLE_CLOSE_LEN);
				out += ci_fmt_style_close(out, in_style);
				in_style = NULL;
			}

			sty = cur;
			while (HAS_DATA && PEEK != ':') cur++;
			sty_len = (size_t)(cur - sty);

			if (HAS_DATA) cur++; /* consume : */

			ENSURE_SPACE(CI_STYLE_OPEN_LEN);
			out += ci_fmt_style_open(out, sty, sty_len);
			in_style = (ci_ptr)1; /* TODO: real style context */

			if (!style_autoclose) continue;
		}

		/* ---- flags ---- */
		char padd_chr = ' ';
		int left_align = 0;

		if (HAS_DATA && PEEK == '-') {
			cur++;
			left_align = 1;
		}

		if (HAS_DATA && PEEK == '0') {
			cur++;
			padd_chr = '0';
		}

		/* ---- width ---- */
		uint16_t w = 0;
		CONSUME_INT(w);

		/* ---- .precision ---- */
		int16_t p = -1;

		if (HAS_DATA && PEEK == '.') {
			cur++;
			p = 0;
			CONSUME_INT(p);
		}

		/* ---- specifier ---- */
		uint8_t spec = 0;
		if (HAS_DATA) spec = NEXT;

		/* ---- format arg ---- */
		if (argi >= arg_cnt) {
			ci_warn_event(dst, "string.format:<!ARG>");
			APPEND("<!ARG>");
			goto spec_done;
		}

		ci_ptr arg = args[argi++];

		/* format into tmp, then pad+copy to output */
		uint8_t tmp[CI_ATOD_MAX_DIGITS];
		size_t n = 0;

		switch (spec) {
		case 's': {
			if (!CI_IS_ANY_STR(arg)) {
				if (CI_IS_ANY_NUMBER(arg))
					goto case_g;
				ci_warn_event(dst, "string.format:<!STR>");
				APPEND("<!STR>");
				goto spec_done;
			}

			const uint8_t *head = ci_str_head(arg);
			n = ci_str_len(arg);

			PADD_COPY(head, n, padd_chr);

			goto spec_done;
		}

		case 'f': {
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!NUM>");
				APPEND("<!NUM>");
				goto spec_done;
			}
			int fdmax = (p >= 0) ? p : 0;

			if(!w){
				ENSURE_SPACE(CI_ATOD_MAX_DIGITS);
				
				out += ci_number_tostring(arg, out, 20, fdmax);
				
				goto spec_done;
			}
			
			n = ci_number_tostring(arg, tmp, 20, fdmax);
			
			break;
		}
		case_g:
		case 'g': {
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!NUM>");
				APPEND("<!NUM>");
				goto spec_done;
			}
			if(!w){
				ENSURE_SPACE(CI_ATOD_MAX_DIGITS);
				
				out += ci_number_tostring(arg, out,
					CI_NUMBER_PRINT_MAX_INT_DIGITS_SCI, 
					CI_NUMBER_PRINT_MAX_FLOAT_DIGITS_SCI);
				
				goto spec_done;
			}
			
			n = ci_number_tostring(arg, tmp, 
					CI_NUMBER_PRINT_MAX_INT_DIGITS_SCI, 
					CI_NUMBER_PRINT_MAX_FLOAT_DIGITS_SCI);
			
			break;
		}
		case 'e': {
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!NUM>");
				APPEND("<!NUM>");
				goto spec_done;
			}
			if(!w){
				ENSURE_SPACE(CI_ATOD_MAX_DIGITS);
				
				out += ci_number_tostring(arg, out, 0, -1);
				
				goto spec_done;
			}
			
			n = ci_number_tostring(arg, tmp, 0, -1);
			
			break;
		}
		
		
		case 'd':{
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!NUM>");
				APPEND("<!NUM>");
				goto spec_done;
			}
			if(!w){
				ENSURE_SPACE(CI_ATOD_MAX_DIGITS);
				out += ci_number_tostring(arg, out, CI_DTOA_INT_ONLY,0);

				goto spec_done;
			}

			n = ci_number_tostring(arg, tmp, CI_DTOA_INT_ONLY,0);

			break;
		}

		case 'x':
		case 'X': {
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!NUM>");
				APPEND("<!NUM>");
				goto spec_done;
			}
			uint8_t hbuf[CI_NUMBER_BUF_HEX];
			n = ci_number_tostring_hex(arg, hbuf, spec == 'X');

			PADD_COPY(hbuf, n, padd_chr);

			goto spec_done;
		}

		case 'o': {
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!NUM>");
				APPEND("<!NUM>");
				goto spec_done;
			}
			uint8_t obuf[CI_NUMBER_BUF_OCT];
			n = ci_number_tostring_oct(arg, obuf);

			PADD_COPY(obuf, n, padd_chr);

			goto spec_done;
		}

		case 'p': {
			uint8_t hbuf[CI_NUMBER_BUF_HEX];
			uintptr_t ptr = (uintptr_t)arg;
			n = ci_number_bits2hex(hbuf, &ptr, sizeof(ptr), 1);
			n = ci_number_strip_leading(hbuf, n);

			PADD_COPY(hbuf, n, padd_chr);

			goto spec_done;
		}

		case 'c': {
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!CHR>");
				APPEND("<!CHR>");
				goto spec_done;
			}
			uint32_t ch = 0;

			if (CI_IS_INT(arg))
				ch = (uint32_t)CI_INT(arg);
			else if (CI_IS_NUMBER(arg))
				ch = (uint32_t)ci_number_to_int(arg);

			ENSURE_SPACE(CI_UTF8_MAX_CHAR_BYTES);
			out += ci_utf8_char(out, ch);

			goto spec_done;
		}

		case 'b': {
			if (!CI_IS_ANY_NUMBER(arg)) {
				ci_warn_event(dst, "string.format:<!NUM>");
				APPEND("<!NUM>");
				goto spec_done;
			}
			uint8_t bbuf[CI_NUMBER_BUF_BIN];
			n = ci_number_tostring_bin(arg, bbuf);

			PADD_COPY(bbuf, n, padd_chr);

			goto spec_done;
		}

		default: {
			PUT('%');
			PUT(spec);
			
			
			goto spec_done;
		}
		}
	
		
		
		/* width + zero-pad for numeric specs */
		{
			int pad = (w > n) ? w - n : 0;
			int sign = (padd_chr == '0' && n > 0 && tmp[0] == '-');
			int skip = sign;  /* bytes to skip in tmp (0 or 1) */

			ENSURE_SPACE(n + pad);

			if (sign) PUT('-');
			if (!left_align) while (pad > 0) { PUT(padd_chr); pad--; }
			memcpy(out, tmp + skip, n - skip);
			out += n - skip;
			if (left_align) while (pad > 0) { PUT(' '); pad--; }
		}

		spec_done:

		if (style_autoclose) {
			style_autoclose = 0;
			ENSURE_SPACE(CI_STYLE_CLOSE_LEN);
			out += ci_fmt_style_close(out, in_style);
			in_style = NULL;
		}

		continue;
	}

	/* commit written bytes */
	size_t _ba = BYTES_ADDED;
	ci_str_put_tail(s, _ba);
	total_written += _ba;

	return (int)total_written;

	#undef HAS_DATA
	#undef PEEK
	#undef NEXT
	#undef CONSUME_INT
	#undef PUT
	#undef BYTES_ADDED
	#undef APPEND
	#undef ENSURE_SPACE
	#undef PADD_COPY
}
