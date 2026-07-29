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
CORRECTED (2026-07-29 survey, db2-927-survey.md): the local 9.2.7 corpus is
**100% WDC3**, zero WDC4 — 9.2.7 sits inside wowdev's WDC3 range (8.1..10.1).
The 8.3.7/9.2.7 row above should read WDC3 only; WDC4 is Dragonflight (10.1+).
The casc test's WDC3||WDC4 check is just defensive.

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
- `db/wire/wdc3.hpp` — the WDC3 format (BfA 8.1 .. early-DF; ALL of 9.2.7).
  `Wdc3Header` (72B), `Wdc3SectionHeader` (40B), `Wdc3FieldStructure` (4B),
  `Wdc3FieldStorage` (24B, the 6 `Wdc3Compression` kinds), a little-endian
  `BitReader`, and `Wdc3Image` — the structural PARSER (locates every block of
  every section) + field-value DECODER (field_raw over all compression kinds,
  pallet/common lookups, sign width helpers). Structurally validated on the
  full 835-file corpus (every file parses; blocks reach within 8B of EOF).
  read_wdc3 (table.hpp) does the typed decode onto the schema.
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

## Stage 3 results — WDC3 READ (2026-07-29)

- wire/wdc3.hpp + read_wdc3 (table.hpp) ship. dbdgen eras extended to ALL 11
  (through tww): **1221 tables** emitted, 0 warnings.
- **Full 9.2.7 typed decode sweep** (scratch wdc3_sweep.cpp over the shadowlands
  manifest, 830 tables): **825 decoded OK** (134 with encrypted sections
  handled), 1 not-locally-present, **4 sparse (NotImplemented)**, **0 schema
  mismatches, 0 other errors**. The dbdgen inline-column count matches every
  WDC3 file's field_count exactly — strong validation of both the emitter and
  the reader.
- Typed spot-check: ManifestInterfaceData id=21 → path "Interface\Cinematics\"
  + name "Logo_1024.avi" (cross-checks the listfile). Non-inline id (id_list),
  WDC2+ relative string offsets, arrays, pallet/common/bitpacked-signed all
  correct.
- Encryption: 138 files have encrypted sections; read() PRESERVES + REPORTS
  them (encrypted_sections()/fully_decoded()), EXCLUDES their (zeroed) rows.
- **Copy tables** materialized as id-cloned records. **Relationships**: the
  non-inline relation column is left at its member default (the relationship
  block is not yet parsed — rare in WDC3, refine if a spot-check needs it).
- Integration test test_db2_corpus_927.cpp: structural parse sweep over ALL db2
  (cheap, no per-table templates) + typed spot-checks on ManifestInterfaceData/
  ChrRaces/SpellName(encrypted). The FULL 830-table typed sweep is NOT baked in
  (one TU = ~2min compile); it lives as the scratch tool.
- GOTCHA: read_wdc3/write_wdc3 bodies are `if constexpr (version >=
  builds::Cata)`-guarded — pre-Cata records carry LocString members the WDC
  field overloads don't model, and those clients never ship WDC3, so the body
  must not instantiate for them.

## Stage 3b — sparse decode + WDC3 write (2026-07-29)

- **Sparse/offset-map decode** (flag 0x01) shipped. Sparse records are located
  by the offset_map ({uint32 abs offset, uint16 size}), id from
  offset_map_ids, and fields are UNCOMPRESSED + SEQUENTIAL — fixed fields at
  natural width, strings inline null-terminated; field_storage bit offsets do
  NOT apply (decode_wdc3_sparse_record walks a byte cursor). Validated on
  spell.db2 (95204 records + 40 encrypted sections): id=5 → "Instantly Kills
  the target.", id=133 (Fireball) → "Throws a fiery ball…". Full sweep now
  **829/829 present tables decode, 0 NotImplemented, 0 errors**.
- **WDC3 canonical WRITE** shipped (write_wdc3). A canonical single-section,
  non-sparse re-encode: every field storage_type None at natural byte width,
  non-inline id in the id_list, strings in a per-section block via WDC2+
  relative offsets. NOT byte-identical (the guarantee is SEMANTIC: write →
  re-read decodes to identical values). Validated: ManifestInterfaceData
  (113346 rec) and ChrRaces (arrays/pallet/bitpacked-signed) both re-decode
  EQUAL — so the compression-kind decoders are proven correct by round-trip.
  A table with encrypted sections is REFUSED (InvalidEntityState — its
  encrypted records were never decoded, so re-encode would lose them).
  fresh_magic now returns wdc3_magic for BfA..pre-DF, so fresh tables are
  writable + unit-testable without a client (synthetic WdcRecord test).
- BUG fixed during write bring-up: string-ARRAY elements store their relative
  offset from their OWN 4-byte position (field_byte + e*4), not the field
  start — the read side must add e*4 too (was resolving all elements from the
  field start). Only affects multi-string-array columns.
- **Remaining limits**: WDC relationship block still not parsed (rare); WDC
  write refuses encrypted tables; no TACT decryption; WDC1/WDC4/WDC5 codecs
  absent (fresh_magic → NotImplemented for legion/DF+).

## Next stages (plan §Stages)

5. WDC1 (7.3.5) + WDC4 + WDC5 (DF+; no local clients; synthetic). WDC4 = WDC3 +
   encrypted_status table; WDC5 = + versionNum/schemaString header prefix.
6. Bindings (weld via dbdgen-emitted shards into the single module) + docs.
7. Deferred: WDC relationship block parse; TACT keys (Salsa20 + CascLib), DBCD
   cross-validation script, lazy/columnar decode, hotfix cache (DBCache.bin)
   out of scope.
