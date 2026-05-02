/*
 * intern_str.c — C string interning helpers for BlueBerry VM.
 *
 * BB_CSTR(vm, "literal") interns a C string literal.
 * Each expansion site gets its own static ci_ptr slot — first call does
 * the hash intern, all subsequent calls return the cached pointer directly.
 *
 * The "" concatenation trick enforces string literals at call sites;
 * passing a char* variable will not compile.
 *
 * Cached pointers are safe across multiple VMs: interned strings are
 * ci_nocnt (no refcounting), live in the tgmemlib arena for the full
 * process lifetime after ci_init(). Strings are readonly so sharing is safe.
 *
 * TODO: ifdef BB_MULTITHREADED_BUILD — disable static cache, fall back to
 * _bb_vm_cconst_slow to avoid races during concurrent intern-on-first-use.
 */

#define BB_CSTR(vm, lit) \
	({ \
		static ci_ptr _cached = NULL; \
		if (__builtin_expect(!_cached, 0)) \
			_cached = bb_vm_istring(vm, "" lit, (uint32_t)(sizeof(lit) - 1)); \
		_cached; \
	})
