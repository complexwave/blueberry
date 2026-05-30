/*
 * blueberry_vm/lib/proto.c — proto namespace
 *
 * proto.set(obj, proto)      — set prototype on a map; proto is map or string
 * proto.get(obj)             — get user-set prototype (null for builtin C types)
 * proto.register(name, map)  — register a named user prototype
 */

/*
 * bb_proto_name2map — resolve a proto argument to a ci_map*.
 * Accepts: ci_map* (returned as-is), string (looked up in registry),
 *          null (returns null). Errors on other types.
 */
static ci_map *bb_proto_name2map(bb_coro *c, ci_ptr arg) {
	if (!arg)
		return NULL;
	if (CI_IS_MAP(arg))
		return (ci_map *)arg;
	if (CI_IS_ANY_STR(arg)) {
		ci_map *proto = (ci_map *)ci_map_find(c->vm->prototypes, arg);
		if (!proto)
			bb_coro_error(c, "proto: '%.*s' not registered",
			              (int)ci_str_len(arg), (char *)ci_str_head(arg));
		return proto;
	}
	bb_coro_error(c, "proto: expected map, string, or null");
	return NULL;
}

/*
 * bb_get_proto — get prototype for any value.
 * Maps: return ->prototype. CI objects: return arena ops prototype.
 * Primitives (int/bool/null): return NULL.
 */
static ci_map *bb_get_proto(bb_vm *vm, ci_ptr obj) {
	(void)vm;
	if (!obj)
		return NULL;
	if (CI_IS_MAP(obj))
		return (ci_map *)((ci_map *)obj)->prototype;
	if (CI_IS_PTR(obj))
		return bb_obj_arena_prototype(obj);
	return NULL;
}

/*
 * bb_typeof — return the typename string for any value, or NULL.
 * Looks up the prototype's "typename" key.
 * Primitives: returns static type name.
 */
static ci_ptr bb_typeof(bb_vm *vm, ci_ptr obj) {
	if (!obj)
		return BB_CSTR(vm, "null");
	if (CI_IS_ANY_NUMBER(obj))
		return BB_CSTR(vm, "number");
	if (CI_IS_BOOL(obj))
		return BB_CSTR(vm, "bool");

	ci_map *proto = bb_get_proto(vm, obj);
	if (proto) {
		ci_ptr name = ci_map_find(proto, BB_CSTR(vm, "typename"));
		if (name)
			return name;
	}

	if (CI_IS_MAP(obj))
		return BB_CSTR(vm, "map");
	if (CI_IS_ANY_STR(obj))
		return BB_CSTR(vm, "string");
	if (CI_IS_ANY_ARR(obj))
		return BB_CSTR(vm, "array");
	if (CI_IS_CLOSURE(obj))
		return BB_CSTR(vm, "function");

	if (CI_IS_REFCOUNTABLE(obj))
		return BB_CSTR(vm, "function");
	
	if (CI_IS_REFCOUNTABLE(obj))
		return BB_CSTR(vm, "object");
	
	return BB_CSTR(vm, "unknown");
}

/* global type(obj) */
static ci_ptr bb_native_type(bb_coro *c, ci_ptr_arg self, ci_ptr a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	return bb_typeof(c->vm, a0);
}

/* proto.set(obj, proto_or_name) */
static ci_ptr bb_proto_set(bb_coro_arg *c, ci_ptr_arg self, ci_ptr obj, ci_ptr proto_arg) {
	ci_map* map_obj = ((ci_map *)obj);

	ci_dec(map_obj->prototype);

	map_obj->prototype = (ci_ptr)bb_proto_name2map((bb_coro *)c, proto_arg);

	ci_inc(map_obj->prototype);

	return obj;
}

/* proto.get(obj) — only returns user-set prototypes on maps */
static ci_ptr bb_proto_getproto(bb_coro_arg *c, ci_ptr_arg self, ci_ptr obj) {
	if (!obj || !CI_IS_MAP(obj))
		return NULL;

	return ((ci_map *)obj)->prototype;
}

/* proto.register(name, proto_map) */
static ci_ptr bb_proto_reg(bb_coro_arg *c, ci_ptr_arg self, ci_ptr name_arg, ci_ptr proto_arg) {
	if (!CI_IS_ANY_STR(name_arg))
		bb_coro_error((bb_coro *)c, "proto.register: first argument must be a string");
	if (!CI_IS_MAP(proto_arg))
		bb_coro_error((bb_coro *)c, "proto.register: second argument must be a map");

	ci_ptr existing = ci_map_find(((bb_coro *)c)->vm->prototypes, name_arg);
	if (existing)
		bb_coro_error((bb_coro *)c, "proto.register: '%.*s' already registered",
		              (int)ci_str_len(name_arg), (char *)ci_str_head(name_arg));

	ci_map *proto = (ci_map *)proto_arg;
	ci_map_put(proto, BB_CSTR(((bb_coro *)c)->vm, "typename"), name_arg);
	ci_map_put(((bb_coro *)c)->vm->prototypes, name_arg, proto_arg);

	return proto_arg;
}

/* proto.all() — return the vm prototype registry map */
static ci_ptr bb_proto_all(bb_coro_arg *c, ci_ptr_arg self) {
	return (ci_ptr)((bb_coro *)c)->vm->prototypes;
}

/* proto.typeof(obj) — return typename string */
static ci_ptr bb_proto_typeof(bb_coro_arg *c, ci_ptr_arg self, ci_ptr obj) {
	return bb_typeof(((bb_coro *)c)->vm, obj);
}

/* proto.of(obj) — get prototype of any value (maps + builtins) */
static ci_ptr bb_proto_of(bb_coro_arg *c, ci_ptr_arg self, ci_ptr obj) {
	return (ci_ptr)bb_get_proto(((bb_coro *)c)->vm, obj);
}

/* ---- registration ---- */

static void bb_lib_proto_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(8);

	static const bb_cfunc proto_lib[] = {
		{ "set",      bb_proto_set,      0 },
		{ "get",      bb_proto_getproto, 0 },
		{ "of",       bb_proto_of,       0 },
		{ "register", bb_proto_reg,      0 },
		{ "all",      bb_proto_all,      0 },
		{ "typeof",   bb_proto_typeof,   0 },
	};

	bb_func2map(vm, ns, proto_lib, sizeof(proto_lib) / sizeof(proto_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "proto", 5), (ci_ptr)ns);
}
