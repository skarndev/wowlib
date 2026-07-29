# ClientDB (DBC/DB2) subsystem

Client-side database files (`DBFilesClient/*.dbc`, `*.db2`), `src/wowlib/db/`,
namespace `wowlib::db`. Plan of record: `~/.claude/plans/rosy-dreaming-beaver.md`.
Stage 1 (WDBC end-to-end) shipped 2026-07-29.

## User decisions (2026-07-29, all confirmed via AskUserQuestion)

1. **Codegen for ALL tables, exhaustive** — typed record structs generated from
   WoWDBDefs per table per layout-range; NOT a dynamic-records API. A runtime
   schema layer still underpins the engine (schema.hpp reflection).
2. **DBD input**: `WOWLIB_DBDEFS_DIR` cache var (local checkout) else a CMake-
   fetched pinned snapshot (`WOWLIB_DBDEFS_PIN` in cmake/DbTables.cmake, pinned
   2026-07-29 @ 61db72dc). Generated headers live in the BUILD tree only.
3. **Python packaging**: all generated tables weld into the ONE wowlib module
   (user chose against a separate extension), sharded TUs for compile parallelism.
4. **Round-trip**: WDBC/WDB2 byte-perfect (corpus-tested). WDC1/3/4/5 writes are
   canonical re-encodes, semantic guarantee (write→re-read equal values);
   encrypted sections always pass through verbatim.
5. **Encryption**: preserve raw keyless sections + report (encrypted_sections()/
   encrypted_ids(), DBCD-style). TACT key injection (Salsa20 + CascLib) deferred.
6. **Validation** (all five adopted): (a) full-corpus decode sweep, (b) WDBC
   byte-perfect corpus round-trip, (c) WDC semantic corpus round-trip, (d)
   known-truth spot checks, (e) manual cross-validation script vs DBCD (never CI).

## Magic ↔ target mapping

| Clients | .dbc | .db2 |
|---|---|---|
| 1.12.1 / 2.4.3 / 3.3.5a | WDBC | — |
| 4.3.4 / 5.4.8 / 6.2.4 | WDBC | WDB2 |
| 7.3.5 | WDBC leftovers | WDC1 |
| 8.3.7 / 9.2.7 | — | WDC3 **and WDC4** |
| 10.2.7 / 11.x | — | WDC5 |

WDB3–WDB6 and WDC2 are Legion/BfA intermediates no last-minor client ships —
out of scope. Reads sniff the file magic; writes re-emit the read magic.
NOTE: the local 9.2.7 ships WDC4 files (test_casc_927_client sniffs both) even
though wowdev's version-range template claims WDC4 starts at 10.1 — trust the
client, survey the 9.2.7 corpus magic histogram before stage 3.

## Architecture (stage 1 shipped)

- `db/annotations.hpp` — `=db::id`, `=db::noninline`, `=db::relation` (spec
  structs in detail, constants as spelling — formats convention).
- `db/locstring.hpp` — `LocString<8|16>` (pre-Cata langstringref: language
  slots + flags), `locale_column_slot(Locale)`, aliases LocString8/16.
  Column slots: enUS 0, koKR 1, frFR 2, deDE 3, zhCN 4, zhTW 5, esES 6, esMX 7,
  ruRU 8, ptBR 10, itIT 11 (9/12–15 unmodeled locales). Vanilla = 8+flags
  (9 cols), 2.1.0.6692+ = 16+flags (17 cols), Cata+ = single std::string.
- `db/schema.hpp` — Column/ColumnType + `schema_of<Record>()` (consteval, via
  reflection over the flat record struct: member TYPE carries width/signedness/
  array/string/locstring; annotations carry id/noninline/relation),
  `record_stride`, `field_slot_count`, `string_slot_count`, TableRecord concept
  (requires statics `version` + `table_name`).
- `db/wire/wdbc.hpp` — WdbcHeader (20B) + `wdbc_magic` (four_cc FORWARD — DB
  magics are plain byte sequences, unlike reversed chunk ids).
- `db/wire/wdb2.hpp` — Wdb2Header (48B) + `wdb2_magic`. Cata..WoD .db2.
  UNVERIFIED (no local client): id-index block (`int32 indices[]` +
  `int16 string_lengths[]`, 6B/id, engaged when max_id != 0) and trailing copy
  table preserved VERBATIM. Adding/removing records of an INDEXED table errors
  (InvalidEntityState — index rebuild unsupported while unverified); in-place
  record edits re-encode fine. Fresh tables emit no index block, stamp build.
- `db/table.hpp` — `Table<Record>`: public `records` vector + read(span)/
  read(fs,key)/write()→FileBuffer/write(fs,key) (WDL-style fs verbs), strings()
  getter. Per-magic codecs are protected members (read_wdbc/write_wdbc,
  read_wdb2/write_wdb2; WDC joins there). **Format dispatch**: reads sniff the
  magic; a loaded table re-emits its source magic; a fresh table's format is
  `fresh_magic()` — by client version (pre-Cata→WDBC, Cata..WoD→WDB2, Legion+
  NotImplemented), overridable by the target path's .dbc/.db2 extension in the
  mixed eras. **Byte-perfect machinery**: StringBlock preserved as decoded
  entries + a `string_offsets_` journal (the original offset of every string
  field, row-major); writes reuse the journaled offset while the value still
  matches (this is what preserves DUPLICATE block entries and mid-entry
  suffix/shared-tail references), else dedup-lookup, else append. Fresh blocks
  seed a leading "" (Blizzard blocks always start with a zero byte). Preserved
  header field_count/record_size are re-emitted (field_count is NOT always
  derivable); fresh tables derive both. write() is const — it works on a COPY
  of the StringBlock.
- ErrorCodes added: `TableTruncated`, `TableMagicUnknown`, `SchemaMismatch`.

## dbdgen (tools/dbdgen, Python 3 stdlib-only)

- `dbd.py` parser (COLUMNS + version blocks; `$id$`/`$noninline,id$`/
  `$relation$`, `<u32>` sizing, `[N]` arrays, `?` unverified, `//` comments,
  BUILD singles/ranges/comma lists, LAYOUT hashes). `targets.py` mirrors
  wowlib::versions + `locstring_langs()`. `emit.py` builds effective members
  per target, collapses consecutive identical targets into ranges
  (version_range.hpp pattern: per-table `<snake>_grid`/`<snake>_pivots` +
  canonicalizing `XRecord<V>` alias + `using X = Table<XRecord<V>>`).
- Emits per-era manifests `manifest_<era>.hpp` with `WOWLIB_DB_TABLES_<ERA>(X)`
  X-macros — the corpus tests sweep via them.
- **Naming**: mechanical CamelCase→snake_case; `_lang` suffix STRIPPED unless it
  collides with a sibling column (ItemRandomProperties has Name + Name_lang →
  `name` + `name_lang`); reserved = C++ keywords ∪ {version, table_name} get a
  trailing underscore (a `Version` column → member `version_`).
- Unsized entries take their type from COLUMNS (float/string/locstring);
  an unsized inline int is a hard per-target error (skips that target, warns).
- CMake: cmake/DbTables.cmake — `WOWLIB_DB_TABLES` (ON), `WOWLIB_DB_ERAS`
  (default vanilla,tbc,wotlk,cata,mop,wod), custom command → stamp →
  `wowlib_db_tables` INTERFACE target carrying the generated include dir.
  ctest `dbdgen_unit` runs the Python unittests.
  GOTCHA: `WOWLIB_DB_ERAS` is a CACHE var — changing its DEFAULT does not
  update an existing build dir (set(CACHE) skips populated entries); pass
  `-DWOWLIB_DB_ERAS=...` or configure fresh. The custom command also does not
  depend on the var, so force a re-run with `rm build/*/generated/db/dbdgen.stamp`.

## Stage 1 results (2026-07-29)

- dbdgen over the pinned snapshot: **251 tables emitted** (vanilla 158, tbc 185,
  wotlk 246), 1069 defs without a classic-era block, 0 warnings.
- Corpus integration tests (test_dbc_corpus_{112,243,335}.cpp + dbc_corpus.hpp):
  every DBC present in all three MPQ clients decodes against its generated
  schema and **writes back byte-identically** — zero failures, first try after
  the locale fix. Zero-byte files (stripped vanilla tables: SpellAuraNames,
  SpellEffectNames, CharacterCreateCameras, SoundCharacterMacroLines) counted
  as `empty`, not decoded. Map spot checks (Azeroth/Kalimdor/Expansion01/
  Northrend) pass on all three.
- Unit: test_db_framework.cpp (hand-written record; schema static_asserts,
  synthetic WDBC, duplicate-entry + suffix-ref byte-perfection, dedup/append,
  fresh-table derivation, error codes, locale slot gating).
- Full suite: 124 cases / 612k assertions green. 3 corpus TUs add ~25s build.

## Gotchas learned

- `std::define_static_string` / `std::define_static_array` live in namespace
  std, NOT std::meta (std::meta::identifier_of etc. are std::meta).
- A `template for` range must be a `static constexpr` local in runtime fns
  (address stability), and needs `static` even inside consteval fns on gcc 16.
- Client locales for the local installs: 3.3.5a opens **enUS**, 1.12.2 **ruRU**
  (repack), 2.4.3 **enGB** — integration_env.hpp holds the constants; the
  corpus tests initially failed by guessing enGB for 3.3.5a.
- DBD classic-era (1.13.x) blocks already use `$noninline,id$` — the engine
  skips noninline members via annotation check, WDBC-era blocks are all inline.
- WDBC `field_count` counts expanded slots (arrays, locstring langs+flags) and
  narrow (u8/u16) columns as one each; real 3.3.5a files agree with the derived
  value everywhere the corpus covered, but we preserve the read value anyway.

## Stage 2 results (2026-07-29)

- WDB2 shipped (wire/wdb2.hpp + read_wdb2/write_wdb2). Synthetic-only:
  test_db_framework.cpp covers plain (no index), indexed+copy-table (verbatim
  round-trip), in-place edit re-encode, indexed-add rejection, fresh Cata→WDB2
  vs fresh WotLK→WDBC magic selection.
- dbdgen eras extended to cata,mop,wod: **448 tables** emitted total (cata 331,
  mop 409, wod 165), 0 warnings — Cata+ single-string locstrings and WDB2-era
  narrow-int/array layouts all emit + compile clean (smoke-checked). No corpus
  test (no Cata/MoP/WoD client installed).

## Next stages (plan §Stages)

3. WDC3/WDC4 read (sections, field_storage compressions, sparse/offset-map,
   id list, copy table, relationships, WDC2+ relative string offsets — the
   error-prone one; encrypted-section preservation + reporting). Survey 9.2.7
   magic histogram first.
4. WDC canonical writes + semantic corpus round-trip.
5. WDC1 + WDC5 (no local clients; synthetic).
6. Bindings (weld via dbdgen-emitted shards into the single module) + docs.
7. Deferred: TACT keys, DBCD cross-validation script, lazy/columnar decode,
   hotfix cache (DBCache.bin) out of scope.
