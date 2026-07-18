# Listfile & custom FileDataID policy

Read when: touching `fs/listfile.hpp`, `fs/csv_listfile.*`, `fs/fdid_allocator.hpp`
or planning the SQL provider.

## Community listfile
- Source: github.com/wowdev/wow-listfile, release asset `community-listfile.csv`
  (~148MB, ~2.2M lines, `fileDataId;filepath`, lowercase, '/'-separated).
  Local copy for tests: `/Users/skarn/WoWModding/Listfiles/community-listfile.csv`
  (`WOWLIB_TEST_LISTFILE` env var; never committed).
- Ingest canonicalizes paths to wowlib form (lowercase + backslash); lookups
  canonicalize inputs, so any spelling works.
- `CsvListfile` keeps two `unordered_map`s (path→id, id→path), `shared_mutex`
  guarded; ~6-8M reserve up front. Known future optimization: string arena +
  open-addressing map, or the SQL provider.

## Custom FileDataIDs
- `FdidAllocator`: monotonic from a configurable start, default **1'000'000'000**
  (Blizzard is at ~6-7M as of 2026; u32 ceiling is ~4.29B — huge headroom both
  ways). Exhaustion → `FdidSpaceExhausted`.
- Custom registrations are kept separate from community data and persisted ONLY
  to a project sidecar: `<project>/.wowlib/custom-listfile.csv` (same CSV
  format). Loading a sidecar bumps the allocator past every id it contains, so
  re-opening a project never re-hands-out ids.
- FileDataID is u32 everywhere — matches the client's root manifest and DB2
  references (the u64 idea from early planning was dropped deliberately).

## Pluggability
`ListfileProvider` concept (`fs/listfile.hpp`): `path_to_fdid`, `fdid_to_path`,
`register_path`, `contains` — all thread-safe by contract. `NullListfile` for
MPQ-era clients. A SQL-backed provider later just satisfies the concept and slots
into `ClientFileSystem<Backend, SqlListfile>` + a facade hook.