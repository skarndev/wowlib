# Listfile & custom FileDataID policy

Read when: touching `fs/listfile.hpp`, `fs/csv_listfile.*`, `fs/fdid_allocator.hpp`
or planning the SQL provider.

## The working-file model (no sidecars — review decision 2026-07-18)
The CSV supplied to `CsvListfile::load` IS the working database: lookups read it,
`register_path` appends `id;path` lines to it (later lines override earlier on
load, so appends are also updates), and `save()` rewrites it canonically (ids
ascending). One file, no separation between community data and customizations.
Consequences:
- Anything loading a listfile it must not mutate should pass a copy (tests do).
- A default-constructed CsvListfile is in-memory only: registrations work but
  don't persist.
- On load the allocator `note_existing`s every id, so custom ids from previous
  sessions (>= custom_fdid_start) bump the cursor and are never re-issued.

## Community listfile
- Source: github.com/wowdev/wow-listfile, release asset `community-listfile.csv`
  (~148MB, ~2.2M lines, `fileDataId;filepath`, lowercase, '/'-separated).
  Local copy for tests: `/Users/skarn/WoWModding/Listfiles/community-listfile.csv`
  (`WOWLIB_TEST_LISTFILE` env var; never committed; integration tests copy it to
  temp before use).
- Ingest canonicalizes paths to wowlib form (lowercase + backslash); lookups
  canonicalize inputs, so any spelling works.

## Custom FileDataIDs
- `fs::detail::FdidAllocator`: monotonic from a configurable start, default
  **1'000'000'000** (Blizzard ~6-7M as of 2026; u32 ceiling ~4.29B). Exhaustion →
  `FdidSpaceExhausted`. FileDataID is u32 everywhere.

## Pluggability
`fs::ListfileProvider` concept: `path_to_fdid`, `fdid_to_path`, `register_path`,
`contains` — all thread-safe by contract. `NullListfile` for MPQ-era clients. A
SQL-backed provider later just satisfies the concept.