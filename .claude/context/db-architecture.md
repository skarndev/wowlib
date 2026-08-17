# ClientDB (DBC/DB2) subsystem

## 2026-08-17: typed per-era Python modules (`wowlib.db.tables.<era>`)

Pre-0.0.3 stub-size fix (db.pyi was 22.9k LOC and choked tooling). NOTHING
is compiled per table — the generic engine is untouched:

- **Runtime** (db_dyn.cpp): `db.tables` + 11 era submodules (versions::
  constants), each with PEP 562 `__getattr__`/`__dir__` off SchemaCatalog;
  first access `type()`s a plain Python subclass of the welded generic
  `Table` whose `__init__` calls base init + the hidden `_open_into(name,
  version)` (move-assigns `DynTable::open`'s result in place — keeps
  subclass identity, isinstance works). Classes cache in the module dict;
  sys.modules gets wowlib.db / .tables / .tables.<era> so dotted imports
  work. `hasattr(era_mod, "X")` mirrors the catalog (ItemSparse: absent
  before the LEGION era snap — cata..wod is ItemSparseLegacy).
- **Stubs**: dbdgen `--py-stubs-dir` emits per-era typed modules
  (`<X>Record(Record)` era-EXACT columns + `<X>(Table[<X>Record])`),
  tables_init, and Table.open Literal overloads returning the PER-TABLE
  UNION of era classes (kept per user choice; the merged-columns classes
  are gone). tools/merge_db_stub.py patches db/__init__.pyi (Generic[R]
  Table via classic TypeVar with PEP 696 `default="Record"`, `__iter__`,
  overload splice; stubgen's own `from . import tables` + `overload`
  imports must not be duplicated) and overwrites db/tables/*.pyi (stubgen
  sees the lazy era modules as EMPTY — it walks module __dict__, not dir()).
  Sizes: db/__init__.pyi 4.3k lines (was 22.9k); eras 1.5k (wod) → 11.9k
  (tww); checkers only load the era you import.
- CMake: DbTables.cmake WOWLIB_DB_PY_STUB_FILES (per-era fragment list);
  bindings stub OUTPUT now db/__init__.pyi + db/tables/__init__.pyi.
- Gate: 160/160 pytest incl. real-client + 3 mypy typing cases (mypy 2.3
  reveals bare `str`/`int`, not `builtins.str`). Docs: guide/db.md +
  python/db/tables.md document the era modules.

Client-side database files (`DBFilesClient/*.dbc`, `*.db2`), `src/wowlib/db/`,
namespace `wowlib::db`. Plan of record: `~/.claude/plans/rosy-dreaming-beaver.md`.
Stage 1 (WDBC end-to-end) shipped 2026-07-29.

## 2026-08-16: THE GENERIC ENGINE (plan: fluffy-twirling-hickey)

Build times forced a redesign: the generated per-era classes were 60–90% of
every build (96 Python shard TUs, 96 C# gen shards, 61k P/Invokes even after
erasure). The schema is DATA now and ONE welded class serves every table:

- **WDBS schema blob** (`dbdgen --schema-blob-out`, format doc in
  `src/wowlib/db/schema_blob.hpp`): 416 KB, all 1221 tables × 11 eras,
  per-range column lists + ERA BITMASKS (never lo..hi — tables skip middle
  eras), disk names preserved. Same bytes serve runtime (`SchemaCatalog`)
  and the future consteval typed validation (`#embed`, gcc `--embed-dir`).
- **SchemaCatalog** (`schema_catalog.{hpp,cpp}`): (table, ClientVersion) →
  `span<const Column>`, era-snapped BY MAJOR. Embedded copy gated by
  `WOWLIB_DB_SCHEMA=embedded|runtime|both` (embedded-only for client-grade
  builds; `from_blob_file` for tools; `.dbd`-dir loader still TODO).
- **DynTable : TableBase** (`dyn_table.{hpp,cpp}`, welded as **`Table`**):
  column-store rows (`detail::ColumnRows` implements RecordSink+Source over
  exact-width per-column buffers; record_bridge.cpp is the parity
  reference — LocString flags via set_int/get_int, get_slot bit-casts
  floats). TableCore gained an EXTERNAL sink/source wiring; codecs
  unchanged, byte-perfect round-trip inherited. Strict public cell
  accessors; `pod_column` zero-copy views; copy/move re-wire.
- **Corpus parity** (`tests/integration/test_dyn_corpus.cpp`): 3.3.5a full
  DBC sweep BYTE-identical; 9.2.7 full DB2 sweep SEMANTIC (write→re-read→
  cell-compare, id-keyed unless ids unpopulated — all-zero id columns occur;
  WDC re-derives copy tables/pallets so byte identity is out of scope BY
  DESIGN, same bar as typed). Catalog-vs-schema_of column parity in
  `tests/unit/test_schema_catalog.cpp`.
- **Python** (`bindings/python/db_dyn.{hpp,cpp}` + welds): `wowlib.db.Table`
  (open/read/write/cells), `Record` row views (`__getattr__` by column
  name; the nurse of a keep_alive must be weakref-able — plain list returns
  cannot carry one), `table.column(name)` zero-copy numpy, `db.table_names()`.
  The 96 shards, facades, RecordVector: DELETED. Module 60 MB → **12 MB**;
  bindings rebuild **8.7 min** (was hours). 154/154 pytest.
- Generated table headers still emitted (C++ typed use; stage-5 consteval
  validation will make them optional convenience). The C# side welds the
  same generic Table; per-range typed FACADES are plain generated C#
  (no interop) packed via welder-csharp's nuget `EXTRA_COMPILE`.

## 2026-07-30 (late): FULL FORMAT COVERAGE + wdc/ restructure

Every era now has a working codec: WDBC (pre-Cata), WDB2 (Cata..WoD,
REAL-CORPUS VERIFIED on 4.3.4 — see below), WDC1 (Legion), WDC3 (BfA/SL,
corpus-verified), WDC4 (10.1..10.2.5), WDC5 (10.2.5+/TWW). Key facts:

- **db/wdc/ subdir** (namespace `wowlib::db::wdc`): `bit_stream.hpp`
  (BitReader/BitWriter), `binary.hpp` (all flavors' structs: Wdc1Header 84B,
  Wdc3Header 72B shared by WDC4, Wdc5HeaderPrefix 132B {version_num,
  schema_string[128]} spliced after the magic, Wdc3SectionHeader 40B,
  WdcFieldStructure/WdcFieldStorage/WdcCompression), `image.hpp/.cpp`
  (WdcImage — flavor-NORMALIZING parser + field decoder), `read.cpp` (Decoder
  class), `write.cpp` (Writer class), `wdc.hpp` (entry points `read_wdc` /
  `write_wdc(magic, ...)`; Table dispatches via `is_wdc_magic`). Old
  `db/wdc3.{hpp,cpp}` deleted. Free-helper piles became class members per the
  house convention (this also answers "why not private methods" — they are
  now).
- **Normalization at parse**: WDC1's single implicit section becomes
  sections[0]; its DENSE min_id..max_id offset map is filtered to present
  entries with explicit ids (owned by the image); its `Bitpacked + flags
  val3&1` signed spelling is rewritten to BitpackedSigned (the writer maps it
  back when emitting WDC1). WDC4/5's per-encrypted-section
  `encrypted_status` id lists are parsed (preferred for
  EncryptedSection.ids); their section tail moves offset_map_ids AFTER the
  relationship block (flag 0x02 restores WDC3 order). The ONLY decoder branch
  left is StringRefMode: WDC1 = block-relative, WDC2+ = field-relative.
- **BUG FIXED (was latent since stage 3): multi-section string resolution.**
  WDC2+ string refs measure the distance inside the client's concatenated
  BLOB (all sections' records, then all sections' strings), NOT inside the
  file. The old reader resolved file-relative → garbage strings in any
  multi-section table whose later sections hold records (e.g. every partially
  encrypted table). The Decoder now maps blob→file through per-section
  geometry. Canary: SpellName (41 sections) id 133 == "Fireball" in
  test_db2_corpus_927.cpp.
- **Relationship maps implemented** (read + write): parsed onto the
  non-inline `$relation$` column ({num,min,max} + {foreign_id,record_index}
  entries, per section; by-id when WDC4+ flag 0x02); the Writer emits one
  entry per kept row + lookup_column_count=1 when the schema has such a
  column. The old "relationship not parsed" limit is gone.
- **fresh_magic ladder** (table.hpp `db2_magic_for_version()`): <Cata WDBC,
  <Legion WDB2, <BfA WDC1, <10.1.0.48480 WDC3, <10.2.5.52432 WDC4, else
  WDC5. TableState gained `wdc5_prefix` (preserved WDC5 header prefix).
- **WDB2 VERIFIED on the real 4.3.4 corpus** (was synthetic-only): the full
  mixed .dbc/.db2 sweep round-trips byte-perfectly (test_dbc_corpus_434.cpp;
  Item.db2 61k records; `sweep_table_mixed` tries .db2 then .dbc; the one
  odd filename is ItemSparse → "Item-sparse.db2"). Reads go through the NEW
  wow-update incremental patch chains — see mpq-load-order.md.
- Synthetic unit proofs for WDC1/WDC4/WDC5 + relationship round-trip live at
  the end of test_db_framework.cpp.
- WDB3–WDB6/WDC2 stay out of scope (no last-minor client ships them).

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
- **TYPE-ERASED CODEC (2026-07-30):** the format engine is NOT templated on the
  record type. `Table<Record>` was collapsing the whole WDBC/WDB2/WDC3 engine
  into ~176k `Table<>` symbols across the 4200 bound tables (the binding binary's
  bulk); it is now a thin facade over non-templated codecs compiled ONCE. The
  .so drops 497 MB → 75 MB (Release -O2), symbol table 287 → 1 MB, peak import
  RSS 220 → 86 MB. The `db/wire/` subdir is gone — its binary structs fold into
  the per-format codec files at `db/` root.
- `db/codec.hpp` — the type-erasure boundary: `RecordSink` (decode target,
  `add()` + `set_int/float/string(record, column, element)`) / `RecordSource`
  (encode source, `get_int/get_slot/get_string`) abstract interfaces the codecs
  drive records through by (column, element) against the runtime schema; plus
  `TableInfo` {version, name, schema}, `TableState` (preserved decode state —
  strings, offset journal, WDB2 blocks, WDC kinds/hashes, encrypted sections),
  `EncryptedPolicy`/`EncryptedSection` (welded, moved here from table.hpp), and
  the schema-derived helpers (record_stride, field_slot_count, id_is_noninline …
  all computed at runtime from the schema span).
- `db/record_bridge.hpp` + `.cpp` — **ERASED (2026-08-15, round 2 of the
  type-erasure)**: `ErasedRecordSink`/`ErasedRecordSource` are NON-templated;
  a consteval `ColumnAccess` table per record ({offset, kind, elem_stride,
  flags_offset}, from the same reflection schema_of() uses) drives a
  kind-switching core compiled once in record_bridge.cpp. The per-record
  residue is `detail::record_ops<Record>`: that descriptor + 7 vector thunks
  (clear/reserve/add/size/at/cat/clone_push — only they know sizeof(Record);
  add/clone instantiate vector growth + Record copy, ~520 B/record, the
  irreducible floor). Templated CONSTRUCTORS on the erased classes keep the
  call sites unchanged. Field access went O(columns) linear `template for`
  scan → O(1) indexed, so wide-table decode (ItemSparse-class) does
  columns× less dispatch work. Measured on db binding shard 56 (-O2):
  −3.8% text / −4% compile alone; −5.8% / −11.4% stacked with the facade
  era-erasure (bindings/python/facade.hpp `def_for_version_overload_erased` —
  the per-(family,expansion) closure became one type, factory passed as a
  function pointer). Corpus green: 53/53 including 1.12.2/3.3.5a byte-perfect
  and 9.2.7 WDC3 semantic round-trips.
- **Round 3 (welder 19d2387, same day): class-erased FIELD properties in the
  nanobind rod** — a directly-declared public arithmetic/enum/std::string
  member binds via offset-capturing `nb::handle` closures through nanobind's
  type-erased `property_install`, so `func_create` instantiates per field TYPE,
  not per (class x type). All three stacked, shard 56 at -O2: text
  1692392 → 1447168 (−14.5%), compile 78.5s → 64.4s (−18%). Full gate:
  welder 64/64 (incl. its nanobind stubcheck), wowlib pytest 153/153 with the
  refreshed venv module (stale-.so trap!), stubs + mypy typing cases green.
  NOTE: .so-level before/after (135 → 130 MB) understates the win — the July
  baseline was built against the July welder pin, and welder main had grown
  shards ~8% since (measured: the same shard TU was 1559088 under the July pin,
  1692392 under current main pre-erasure).
- **Round 4 (2026-08-15, same day): TableBase + TableCore + RecordVector —
  the .so HALVED, 130 → 60 MB (text 100.2 → 44.8 MB); shard 56
  1447168 → 661544 (−61% vs the day's baseline), compile 78.5 → 52 s.**
  - `db/table_core.{hpp,cpp}`: TableCore (non-templated engine — every method
    body compiled once, driven by TableInfo + RecordOps; unwired core errors
    with InvalidEntityState) + the ONE welded `TableBase` carrying the whole
    method surface. `Table<Record>` (hand-written-record path) is a thin typed
    shell over the same core; its copy/move REWIRE the core (the one owner
    obligation).
  - dbdgen: family supertype `X_ : TableBase`; per-era `XTable<V> : X_` holds
    only records + identity + wiring special members. Methods are INHERITED in
    every language — `read`/`write` no longer appear in tables.pyi at all.
    welder handled the 2-level welded base chain unchanged.
  - `records` is a **live RecordVector view** (bindings/python/record_vector.hpp):
    erased over {vector*, RecordOps*, element type object}; `__getitem__` wraps
    elements IN PLACE via nb::inst_reference (keep-alive parent = the table),
    append/extend/setitem/delitem via new RecordOps thunks (push_copy/
    assign_at/erase_at). FIXES the old semantics where the stl caster returned
    a list of COPIES and `t.records[0].id = 5` silently mutated a detached
    copy. Rebind (`t.records = [...]`) = clear + copy-in. Caveat: growth may
    reallocate — re-index rather than holding element refs across appends.
  - Facade fully erased (facade.hpp FamilyEra + db_facade.hpp): for_version
    overloads and fallback dispatch via registered TYPE OBJECTS (calling the
    class object constructs), AnyX folded from the same handles; formats
    families share the erased helpers. No template instantiates per family.
  - Gate learnings: a public method returning a non-welded type (TableBase::
    core()) trips the bindability gate — `mark::exclude` is the escape;
    `nb::type<T>()` returns handle, not object (borrow it).
  - ~~Pre-existing: welder never bound C++ default arguments~~ **FIXED in
    welder dda6d03 (same day)**: both Python rods synthesize one truncated
    overload per omissible arity (the language applies the real default —
    P2996 cannot splice the defaulting expression). `t.write()` now works
    bare from Python, applying EncryptedPolicy::Preserve. kwargs work on
    every arity; keep_alive rides the full arity only.
  - Verified: dbdgen unit 17, C++ db+corpus 73/73 (byte-perfect + WDC
    semantic), pytest 153/153, mypy typing cases green, plus direct probes of
    in-place mutation, iteration, del, rebind, for_version, the isinstance
    chain (concrete → family → TableBase) and clean unwired-base errors.
- `db/wdbc.{hpp,cpp}` — WdbcHeader (20B) + `wdbc_magic` (four_cc FORWARD — DB
  magics are plain byte sequences, unlike reversed chunk ids) + the non-templated
  `read_wdbc`/`write_wdbc(TableInfo, ..., RecordSink&/Source, TableState&)`.
- `db/wdc3.{hpp,cpp}` — the WDC3 format (BfA 8.1 .. early-DF; ALL of 9.2.7).
  `Wdc3Header` (72B), `Wdc3SectionHeader` (40B), `Wdc3FieldStructure` (4B),
  `Wdc3FieldStorage` (24B, the 6 `Wdc3Compression` kinds), a little-endian
  `BitReader`/`BitWriter`, and `Wdc3Image` — the structural PARSER (locates every
  block of every section) + field-value DECODER (field_raw over all compression
  kinds, pallet/common lookups). `read_wdc3`/`write_wdc3` do the schema-driven
  decode/canonical encode (bit packing, pallet/common re-derivation, copy-table
  dedup) through the bridge. Structurally validated on the full 835-file corpus.
- `db/wdb2.{hpp,cpp}` — Wdb2Header (48B) + `wdb2_magic`. Cata..WoD .db2.
  UNVERIFIED (no local client): id-index block (`int32 indices[]` +
  `int16 string_lengths[]`, 6B/id, engaged when max_id != 0) and trailing copy
  table preserved VERBATIM. Adding/removing records of an INDEXED table errors
  (InvalidEntityState — index rebuild unsupported while unverified); in-place
  record edits re-encode fine. Fresh tables emit no index block, stamp build.
- `db/codec_detail.hpp` + `db/codec_common.cpp` — the shared fixed-stride
  (WDBC/WDB2) inline-field decode/encode + the byte-perfect `StringPool`,
  compiled once.
- `db/table.hpp` — `Table<Record>`: public `records` vector + read(span)/
  read(fs,key)/write()→FileBuffer/write(fs,key) (WDL-style fs verbs), strings()
  getter. It builds a Typed{Sink,Source} over `records` and calls the codec
  functions with `info()` (schema + version + name) and `state_`. **Format
  dispatch**: reads sniff the
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

- WDB2 shipped (binary/wdb2.hpp + read_wdb2/write_wdb2). Synthetic-only:
  test_db_framework.cpp covers plain (no index), indexed+copy-table (verbatim
  round-trip), in-place edit re-encode, indexed-add rejection, fresh Cata→WDB2
  vs fresh WotLK→WDBC magic selection.
- dbdgen eras extended to cata,mop,wod: **448 tables** emitted total (cata 331,
  mop 409, wod 165), 0 warnings — Cata+ single-string locstrings and WDB2-era
  narrow-int/array layouts all emit + compile clean (smoke-checked). No corpus
  test (no Cata/MoP/WoD client installed).

## Stage 3 results — WDC3 READ (2026-07-29)

- binary/wdc3.hpp + read_wdc3 (table.hpp) ship. dbdgen eras extended to ALL 11
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
- **WDC3 canonical WRITE** shipped (write_wdc3), BITPACKED. Integer columns are
  bitpacked to the minimum width their actual values need (BitpackedSigned for
  signed columns — two's-complement low bits, sign-extended on read); floats and
  string refs stay uncompressed 32-bit and byte-aligned. A binary::BitWriter
  mirrors BitReader. plan_wdc3_fields() scans the records for per-field int
  ranges (unsigned_width/signed_width) then assigns tight bit offsets. Single
  non-sparse section, non-inline id_list, strings via WDC2+ relative offset.
  NOT byte-identical (guarantee is SEMANTIC: write → re-read → identical
  values). Validated: ManifestInterfaceData (string-only, ~same size),
  ChrRaces (int columns → 77% of original, EQUAL round-trip), synthetic
  small-int record → record_size 1 byte (2-bit pack). fresh_magic returns
  wdc3_magic for BfA..pre-DF so fresh tables are writable + unit-testable
  without a client.
- **Encrypted tables preserve their original image VERBATIM on write** when
  they still have keyless sections: read_wdc3 keeps the raw bytes (wdc_original_)
  when any section is keyless; write() re-emits them byte-identically. This is
  the FALLBACK for files you can't decrypt.

## Encryption model — CORRECTED (2026-07-29, ref: InternWoWTools/TACT.Builder + DBCD.IO)

The reference tool that rebuilds itemsparse.db2 etc. does NOT preserve encrypted
blocks by re-encoding around them. It runs WITH the TACT keys, so CascLib
delivers fully-DECRYPTED .db2 bytes, and DBCD writes a normal single plaintext
section (TactKeyLookup 0, re-derived compression). "Editing an encrypted table"
= decrypt-with-keys → all records present → canonical write. There is no
frozen-layout in-place rebuild (I nearly built one — don't).
- **Section-skip heuristic (DBCD, now ours)**: a WDC3 section is only
  undecodable when `tact_key_hash != 0 AND its records are all zero` (key absent
  ⇒ storage shipped zeros). A key-flagged section with NON-zero records was
  decrypted by the storage and decodes normally. **My reader's old "skip on
  tact_key != 0" was a BUG** — it dropped 65 already-decrypted sections across
  ~24 tables in the 9.2.7 corpus (e.g. location.db2's 2 sections = ~103k rows).
  Fixed with `detail::all_zero()` (schema.hpp). encrypted_sections() now reports
  only the genuinely keyless sections.
- **TACT keys**: FileSystem::import_keys(text) (community "KeyName KeyHex" lines,
  wraps CascImportKeysFromString) + FileSystem::add_encryption_key(u64, 16 bytes)
  / the same on CascStorage. Register keys → read_file returns decrypted bytes →
  every section decodes → editable via the canonical writer. MPQ returns
  NotSupported. (Locally we can't fully exercise decryption of the 1283 keyless
  sections — the repack lacks that encrypted BLTE data — but the API is wired and
  the reader fix is validated on the 65 already-decrypted sections.)
- So editing flow: keyed/plaintext table (encrypted_ empty) → canonical write
  applies edits. Table with residual keyless sections → verbatim preserve
  (register keys to edit).

## Copy-table re-derivation on write (2026-07-29)

Corpus survey: WDC copy tables are HUGE — 1.16M copy rows across 239/835 tables;
spell 65%, item 73%, spellvisualkit 99% copies. The reader EXPANDS copies into
full records, so a naive write (all rows, no copy table) balloons wide tables
10-600× → the client crawls loading them (the TACT.Builder itemsparse slowness).
write_wdc3 now RE-DERIVES the copy table: records identical in every non-id
field (wdc_value_key, position-independent) are stored once + a {new_id,src_id}
copy entry. Only when record_size >= 8 (a copy entry is 8 bytes — DBCD's
threshold; narrow rows stay expanded). Round-trip preserved (reader re-expands);
reals-before-copies order matches the decode order so records==reread holds for
single-section tables. Measured: Curve 99%-copies → 62% of original; narrow
tables unaffected. GOTCHA: string relative offsets are position-dependent, so
dedup is by VALUE (wdc_value_key), not encoded bytes.

## Pallet encoding on write — SHIPPED (2026-07-30)
DBCD reuses each column's ORIGINAL compression KIND (reader.ColumnMeta) and
recomputes widths from data. We do the same for Pallet/PalletArray: read_wdc3
captures wdc_kinds_ (per-inline-column storage_type); write_wdc3 keeps that kind
for INT and FLOAT columns Blizzard palleted (graphics tables pallet floats
heavily), builds a distinct-value table + bitpacked index (index width =
unsigned_width(distinct_count)), emits a pallet_data block (per field, in field
order — reader accumulates additional_data_size for the base), and writes one
index per record (elem_bits wide) rather than the value. The reader already
decoded pallet, so round-trip holds. Bitpacked/None columns unchanged.
- Effect: bloated (>110%, >20KB) fully-decoded tables dropped 32 -> 2; aggregate
  73% of Blizzard's total (was 80%). DissolveEffect 274%->104%, LightData
  183%->92% (beats Blizzard), CreatureModelData 210%->98%, AnimationData
  162%->99%. All round-trip value-equal.
- GOTCHA: pallet must cover FLOAT columns, not just int — the worst outliers
  (graphics tables) pallet floats. wdc_u32() bit_casts float slots.
- Remaining outlier after pallet: CreatureDisplayInfoExtra (137%) used Common.

## Common encoding + inline-id fix — SHIPPED (2026-07-30)
- Common (CompressionType 2): the record stores NOTHING; the value lives in a
  per-id {id,value} table (sorted, binary-searched by the reader) with a default
  (field_storage val1). write_wdc3 reuses the original Common kind for scalar
  int/float columns, picks the most-frequent value over the KEPT (real) rows as
  the default, and emits only the differing rows as entries (copies inherit, so
  only reals need entries). common_data block sits after pallet_data.
- INLINE-ID FIX (latent bug): the writer always forced a non-inline id_list +
  flag 0x04, but tables whose DBD marks $id$ WITHOUT $noninline$ (e.g.
  CreatureDisplayInfoExtra, flags 0x00) keep the id as an inline record field.
  Forcing non-inline desynced the layout and corrupted such tables on write.
  Fixed: wdc_id_is_noninline()/wdc_id_field_index() consteval — inline-id tables
  now write flags 0x00, id_index = the id's field index, and NO id_list.
- Result: bloated (>110%,>20KB) fully-decoded tables 2 -> 0; aggregate 70% of
  Blizzard's total. Every fully-decoded table now writes <=~110% of original.
- ROUND-TRIP GUARANTEE refined: write coalesces duplicate rows into copy
  entries, so a MULTI-section table can be REORDERED (a section-1 row that dups a
  section-0 row becomes a copy). The guarantee is same-SET-by-id, not vector
  order (WDC rows are id-keyed; DBCD reorders too). Single-section tables keep
  order. The semantic round-trip test sorts by .id before comparing.
- SHIPPED: EncryptedPolicy::Drop on write() writes a keyless table as a plain
  WDC3 of just its decoded rows (keyless rows discarded, output unencrypted) —
  the way to apply edits to a table you can't fully decrypt. Default is
  Preserve (verbatim). 9.2.7 has 1283 keyless sections, so this is real and
  tested (SoundKit: Preserve → byte-identical; Drop → plaintext of decoded rows).
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

## Python bindings (STARTED 2026-07-30)

User decisions: bind ALL tables sharded across vanilla/tbc/wotlk/shadowlands
(the locally-testable eras), everything in the ONE wowlib module. DF/TWW
schema-only later (no WDC5 reader). Expect a very long compile (~10k+ welded
classes); shard the instantiations across many TUs.

**Binding pattern (mirror WMO/M2, per bindings-notes.md)**: welder welds a
class-template instantiation through a namespace-scope ALIAS whose identifier is
the target name (`using MapWotlk = Map<versions::wotlk>`). A versioned welded
entity needs (a) a welded empty BASE (weld_as the family name) giving the
per-version classes a common supertype + single-welded-base for nanobind, (b) a
per-version wrapper inheriting the base + a NON-welded mixin whose public members
flatten on. So per db table: welded `MapRecord*` structs, welded `MapBase`
(weld_as "Map"), `Map<V> : Table<MapRecord<V>>, MapBase`, range aliases, a
for_version facade (facade.hpp is generic). dbdgen emits all of this + sharded
instantiation TUs; the module walk needs the aliases visible (umbrella +
instantiations/*.hpp like WMO).

**DONE (foundation)**: `Table<Record>` is now a NON-welded MIXIN (removed the
class weld; kept member annotations — getters/no_reassign/docs — which welder
reads off the declaring class when flattening). `EncryptedSection` hoisted to
`wowlib::db` namespace scope (Record-independent → welded once, shared). C++ db
tests still pass (32 cases).

**REMAINING**: dbdgen emits welded records + MapBase/Map<V>/aliases + facade
registration; umbrella pre-declares `wowlib::db`; opaque_gen registers db record
vectors/arrays; sharded instantiation TUs; stubs OUTPUT entries; check.py; then
the (long) wowlib_py build + a Python round-trip test. Prove ONE table (Map)
end-to-end before generating 1221×.

## Python bindings — RESOLVED + shipped (2026-07-30)

The namespace-surfacing blocker below was fixed the same day (commits
570a895..c14baae): tables surface as `wowlib.db.tables`, sharded TUs,
grid-gated for_version facade + AnyX unions, full 4-era set, then the Ninja
release preset + memory-aware job pool fixed the 30-min build. On 2026-07-30
(late) the default era set for BOTH C++ and Python builds became ALL ELEVEN
eras (cmake/DbTables.cmake; WOWLIB_DB_SHARDS default 96 keeps per-shard
density at the proven level) — every era now has a codec, so the whole grid
is meaningful from Python. Historical record of the blocker follows.

## Python bindings — BLOCKED on a welder namespace-surfacing mystery (2026-07-30) [RESOLVED, see above]

The C++ binding surface is DONE and committed (dbdgen emits rowbase/tablebase
welded supertypes + wrapper + records; commits d000cd3, 63ea0c2). The wiring to
surface `wowlib.db` in the module was attempted (db.hpp + umbrella db decl +
module/opaque includes + CMake links) and REVERTED because db never surfaces at
runtime, despite:
- `wowlib.abi3.so` builds clean (~6 min) and CONTAINS 124 db symbols (rowbase,
  MapRecord, EncryptedSection registration code compiled in).
- Reflection is IDENTICAL to `fs`: db is member 26 of `^^wowlib` (fs is 23), has
  1 doc annotation (fs has 1), and every welder gate passes — verified by direct
  consteval probes: `member_bound(db)=1`, `namespace_has_bound(db)=1`, and the
  ACTUAL walk predicate `marker_resolution::namespace_participates(^^wowlib::db,
  py, policy_of(^^wowlib), ^^wowlib) = 1` (same as fs).
- Yet `wowlib.db` is absent from the module (`dir(wowlib)` submodules =
  [versions, formats, fs] only), sys.modules has no wowlib.db*, and no db type
  exists anywhere in the module tree. No import error/warning. The walk simply
  never emits db's submodule despite the compile-time gate saying it should.
- Moving the db decl into the umbrella (exactly like fs/formats) did NOT fix it.
  `formats::common` (undoc'd, welded content) DOES surface, so doc isn't the
  factor and content isn't missing.

This is a genuine welder-integration mystery (possible welder bug or a subtle
interaction the reflection probes don't capture — e.g. the WELDER_MODULE walk's
`^^wowlib` enumeration at the macro point differing from a standalone
`members_of`, a visited-set, or ordering). Needs welder-author input or stepping
through the generated walk code. Fast repro: the consteval probes in the session
scratchpad (db_gate.cpp / db_part.cpp) compile in ~seconds and show
participates=1 while the 6-min module build shows no db submodule.

Everything ELSE for the bindings is ready: the pattern (welded base + wrapper +
alias) compiles, the collision-proof rowbase/tablebase namespace scheme works,
and Table is a non-welded mixin. Once surfacing is fixed, the remaining work is
mechanical: dbdgen emits per-range aliases + a facade, shard the instantiations,
extend stubs, add check.py. Scope: all tables, sharded, eras vanilla/tbc/wotlk/
shadowlands.
