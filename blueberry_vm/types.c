/*
 * types.c — named prototype registry for BlueBerry VM.
 *
 * Named prototypes serve two purposes:
 *   - give C objects a type identity (registered once, looked up by name)
 *   - foundation for future shape-caching optimisations on user types
 *
 * All prototype maps are ci_nocnt (arena-owned, not refcounted).
 * Each prototype stores a "typename" key pointing back to its own name
 * so objects can identify their type at runtime.
 *
 * API:
 *   bb_proto_register(vm, "name")  — create and register a named prototype
 *   bb_proto_get(vm, "name")       — look up a registered prototype (or NULL)
 *   bb_proto_new_map(vm, "name", size) — create a map whose prototype is "name"
 *
 * Internal:
 *   bb_proto_register_cistr(vm, ci_ptr name) — same as above but takes interned ptr
 */

#define bb_obj_arena_prototype(obj) ((ci_map *)tg_ptr_arena(obj)->ops.prototype)

static void bb_proto_register_cistr(bb_vm *vm, ci_map *proto, ci_ptr name) {
	ci_ptr existing = ci_map_find(vm->prototypes, name);
	if (existing) {
		bb_error("bb_proto_register: '%.*s' already registered",
		         (int)ci_str_len(name), (char *)ci_str_head(name));
	}

	ci_map_put(proto, BB_CSTR(vm, "typename"), name);
	ci_map_put(vm->prototypes, name, (ci_ptr)proto);
}

/*
 * Set the prototype on all existing arenas of a tag, plus the allocator
 * template so future arenas inherit it.  Called after type registration
 * when the prototype map becomes available at VM init time.
 */
static void bb_set_arena_prototype(uint16_t tag, ci_map *proto) {
	ci_alloc->ops[tag].prototype = proto;
	for (tg_arena_t *ar = ci_alloc->heads[tag]; ar; ar = ar->next) {
		ar->ops.prototype = proto;
	}
}

static ci_map *bb_proto_register(bb_vm *vm, const char *name) {
	ci_ptr n = bb_vm_istring(vm, name, (uint32_t)strlen(name));
	
	ci_map *m = ci_map_ident_new(16);
	ci_nocnt(m);
	
	bb_proto_register_cistr(vm, m, n);
	return m;
}

static ci_map *bb_proto_get(bb_vm *vm, const char *name) {
	ci_ptr n = bb_vm_istring(vm, name, (uint32_t)strlen(name));
	return (ci_map *)ci_map_find(vm->prototypes, n);
}

/* Create a map whose prototype is the named prototype. */
static ci_map *bb_proto_new_map(bb_vm *vm, const char *name, uint32_t size) {
	ci_map *proto = bb_proto_get(vm, name);
	if (!proto) {
		bb_error("bb_proto_new_map: prototype '%s' not registered", name);
	}

	ci_map *m = ci_map_ident_new(size);
	m->prototype = (ci_ptr)proto;

	return m;
}

static bb_metaproto *bb_proto_register_meta(bb_vm *vm, const char *name) {
	bb_metaproto *mp = tg_alloc_linked(ci_alloc, CI_MAP, sizeof(bb_metaproto));
	if (!mp) return NULL;

	ci_map *m = &mp->map;
	if (!ci_map_init(m, 16)) return NULL;
	m->hashcmp = ci_hashcmp_identity;
	m->gc.flags |= CI_TAG_METAPROTO;
	ci_nocnt(m);

	mp->op_add = NULL;
	mp->op_sub = NULL;
	mp->op_mul = NULL;
	mp->op_div = NULL;

	ci_ptr n = bb_vm_istring(vm, name, (uint32_t)strlen(name));
	bb_proto_register_cistr(vm, m, n);
	return mp;
}
