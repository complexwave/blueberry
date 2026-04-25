# ci_map TEST PLAN

## test_basic
Allocation, tag checks, refcount, set/get/delete, clear, iteration.
- `ci_map_new` returns non-NULL, correct tags (`CI_IS_ANY_MAP`, `CI_IS_MAP`), refcountable, refcnt=1
- `ci_map_new(0)` and `ci_map_new(1)` clamp to `CI_MAP_MIN_BUCKETS`
- `ci_map_len` returns 0 on fresh map
- `ci_map_set` / `ci_map_get` roundtrip with distinct pointer keys
- `ci_map_get` returns NULL for missing key
- `ci_map_set` same key twice → value replaced, used unchanged
- `ci_map_delete` removes key, `ci_map_get` returns NULL after
- `ci_map_delete` on missing key returns 0
- `ci_map_clear` resets used to 0, all keys gone
- `ci_map_next` iterates all entries (count matches used)
- `ci_free` triggers destructor (no crash, space freed)

## test_resize
Trigger automatic resize at load threshold; verify all entries survive.
- Insert until `used > used_limit` fires resize
- All pre-resize entries accessible after resize
- buckets doubled, used_limit updated

## test_collision
Keys that hash to same initial bucket → correct Robin Hood displacement and retrieval.
- Manufacture a small map (8 buckets) and insert enough keys to force collisions
- Verify all keys found; PSL distribution reasonable

## test_delete_shift
Backward-shift correctness: delete mid-chain, verify neighbours still findable.
- Insert cluster of 4+ colliding keys
- Delete the first-inserted (lowest PSL) → triggers shifts
- Remaining keys still return correct values

## test_stress
10k random pointer insertions, lookups, deletions; verify consistency.
- Track expected state in parallel array
- No crashes, no lost entries, used count matches expected

## test_iteration
`ci_map_next` cursor iterates exactly `used` unique entries with no duplicates.

## test_refcount
`ci_inc` / `ci_dec` on map itself; dec-to-zero calls destructor.
