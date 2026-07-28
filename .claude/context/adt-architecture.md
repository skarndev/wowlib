# ADT subsystem architecture (plan of record)

Terrain map tiles (`src/wowlib/formats/adt/`). Follows the WMO/WDT/WDL/M2
recipe. Decisions fixed with the user 2026-07-26, grounded in a FULL-corpus
raw-chunk survey of both test clients (scratchpad `adt_survey.py`).

## User decisions (do not relitigate)

- **ONE version-agnostic entity, not ADTRoot/ADTTex/ADTObj.** `ADT<V>` holds
  EVERY chunk; the split into physical files (Cata+: root/_tex0/_obj0/_obj1/
  _lod) is a SERIALIZER concern, opaque to the user. Adding a texture =
  appending to `ADT.textures` (or `diffuse_texture_ids`) — no root-vs-tex
  if/else. Same for the 256 `MapChunk<V>` cells: each holds ALL its
  sub-chunks; the serializer routes each to its file.
- **Alphamaps ALWAYS stored decompressed** (the edit surface): MCAL is decoded
  on read (2048 4-bit / 4096 8-bit / RLE), stored as 4096 bytes per layer,
  re-encoded on write.
- **Semantic round-trip, NOT byte-perfect** (like M2, unlike WMO/WDT/WDL). The
  guarantee is `parse(write(parse(x))) == parse(x)`, tested by a reflection-
  driven bitwise value diff (M2's `diff_value` pattern), NOT memcmp. Forced by:
  greedy RLE re-encode reproduces Blizzard's exact bytes only ~30–48% of the
  time (survey), and MHDR/MCIN/MCNK-header offsets are all derived. Unknown
  chunks are still preserved. Writer layout policy is one replaceable function.
- **Liquids FULLY structured** now: MH2O (WotLK+) decoded to per-cell
  instances/heightmaps/exists-bitmaps via the offset-multiplier method; MCLQ
  (pre-WotLK) decoded to its flag-driven vertex union + tile grid. Both
  SelfSerializing, like WMO's MLIQData.

## Survey findings (3.3.5a: 5744 tiles / 1.47M MCNKs; 9.2.7: full corpus)

Baked into the models as ground truth / wowdev corrections:
- **MHDR** (monolithic): all offsets are relative to the MHDR PAYLOAD start
  (i.e. chunk-data begin) and point at the target chunk's HEADER (fourcc), not
  its payload — 100%. mfbo/mh2o/mtxf offsets are 0 when the chunk is absent;
  presence tracks MHDR flags (mhdr_MFBO=1). Fully DERIVED → stamp on write.
- **MCIN** (monolithic, <Cata): 256 records; offset = ABSOLUTE at the MCNK
  header, size = INCLUDES the 8-byte chunk header (256/256 every file); flags
  and asyncId always 0. Fully DERIVED → stamp on write. Gone in split files.
- **MCNK header** (128 bytes): sub-chunk offsets (ofsMCVT/ofsMCNR/ofsMCLY/
  ofsRefs/ofsAlpha/ofsShadow/ofsLiquid/ofsMCSE/ofsMCCV/ofsMCLV) are relative to
  the MCNK CHUNK START (fourcc position) and point at the sub-chunk HEADER.
  ofsShadow is only valid when flag has_mcsh (0x1) — the "mismatch" cases in
  the survey are exactly the chunks WITHOUT the flag (stale pointer). MapChunk
  stamps all of these locally on write.
- **MCNR** (<Cata): declares size 435 but is followed by **13 undeclared
  padding bytes** (100% of 1.47M chunks) before the next sub-chunk — the reader
  must consume 448. Cata+ MCNR declares the full 448. The 13 bytes are a near-
  constant pattern; preserve them per cell for the semantic diff.
- **MCAL**: the sub-chunk's OWN size field is UNRELIABLE (25% disagree with the
  header); authoritative size is `MCNK.header.sizeAlpha`. Mode is chosen by
  MCLY.flags (0x200 compressed) + WDT MPHD (0x4/0x80 = big_alpha/8-bit):
  compressed→RLE, else 4096 (big_alpha) or 2048 (4-bit). MCNK flag 0x8000
  (do_not_fix_alpha) governs the 63→64 "fix"; wowlib always stores the fixed
  64×64 form. Old maps in a 9.2.7 install still ship 2048 4-bit (big_alpha not
  set) — the WDT flag, not the client era, decides.
- **MCSE**: pre-Cata records are 28 bytes (0x1C), not the 76/52-byte alpha
  layouts; size field is the record count × 28.
- **MCLQ** (pre-WotLK): present when MCNK flags lq_* (bits 2..5) set; size from
  header.sizeLiquid (>8 means present). SWVert/SOVert/SMVert union by liquid
  type; verts[9*9] + tiles[8][8] + 2 flow vectors.
- **MH2O**: 256 SMLiquidChunk headers first, then instances immediately after
  (offset 256*12), then the referenced data (attributes/exists/vertex). Vertex
  format resolved by the offset-multiplier method (survey confirms multipliers
  1/5/8/9 = cases 2/0/1/3). 9.2.7 ocean tiles: liquid_type 2, lo>=42, no vertex
  data. offset_attributes==0 legitimately omits attributes.
- **Top-level chunk order** (3.3.5a root): MVER MHDR MCIN MTEX MMDX MMID MWMO
  MWID MDDF MODF [MH2O] 256×MCNK [MFBO] [MTXF]. (MH2O sits before MCNK.)
- **Split distribution** (9.2.7): root = MVER MHDR [MH2O] 256×MCNK(header +
  MCVT/[MCLV]/[MCCV]/MCNR/[MCLQ]/MCSE) [MFBO]; tex0 = MVER MAMP MDID MHID
  256×MCNK(headerless: MCLY/[MCSH]/[MCAL]) [MTXP]; obj0 = MVER [MDDF] [MODF]
  256×MCNK(headerless: [MCRD]/[MCRW]); obj1 = MVER [MLFD] MLDD MLDX MLMD MLMX;
  lod = MVER MLHD MLVH MLLL MLND MLVI MLSI MLLD MLLN MLLI MLLV. Only the ROOT
  MCNK carries the 128-byte header; tex/obj MCNK are bare sub-streams. MHDR
  offsets are ~all zero in split files (client uses split layout).

## Design

- **Namespace/dir mirror** (user rule): `formats/adt/` (namespace `adt`):
  - `boundaries.hpp` — adt_versions grid (all 11), wire version 18, layout
    pivots + per-family canonicalization pivots, `AlphaFormat` context enum.
  - `chunks/` (`adt::chunks`) wire structs by family: `header.hpp` (SMChunk
    MCNK-header, MFBO), `texture.hpp` (SMLayer/MTXF/MTXP/MTCG/MAMP + flag
    enums), `liquid.hpp` (SMLiquidChunk/SMLiquidInstance/MH2O attributes;
    MCLQ SLVert union/tiles/flow), `object.hpp` (blend-mesh MBMH/MBBB/MBNV/
    MBMI, MWDR/MWDS), `lod.hpp` (MLHD/MLVH/MLLL/MLND/MLLN records). MDDF/MODF
    reuse `common/map_placements.hpp` SMDoodadDef/SMMapObjDef.
  - `map_chunk.hpp` (`adt`) — `MapChunk<V>`: the per-cell entity. NOT a
    generic ChunkedEntity — it owns a custom `read_slice(span, FileKind)` /
    `write_slice(buf, FileKind, AlphaFormat)` (header + quirky sub-chunks +
    offset stamping + alpha decode/encode). Members carry an `in_file`
    routing annotation (root/tex/obj).
  - `liquid.hpp` (`adt`) — `MapChunkLiquid` (one cell's MH2O) SelfSerializing,
    `MH2OData` (the 256-cell top-level chunk), `MCLQData`.
  - `adt.hpp` (`adt`) — `ADT<V>` unified entity + `ADTBase` + assembly fs
    read/write. Custom split-file serializer (`adt::detail`): per physical
    file, walk `in_file`-matching + version-active members, route MCNK slices
    to each file, stamp MHDR/MCIN after the root buffer is built.
  - `convert.hpp` — supported_versions<ADT>.
- **`in_file` annotation** (adt-local or annotations.hpp): tags each top-level
  and MCNK sub-chunk member with its physical file (root/tex/obj/lod). For
  <Cata the serializer runs a single "monolithic" pass accepting every kind.
- **Derived-field stamping**: MHDR (offsets+flags) and MCIN via a whole-file
  `patch_file` pass over the root buffer; MCNK-header offsets/sizes/counts
  stamped by MapChunk::write_slice locally; all derived fields mark::exclude'd.
- **AlphaFormat context**: MCAL decode/encode needs the WDT MPHD big-alpha
  bit. `ADT::read/write(fs, key)` resolves the sibling WDT (derived from the
  tile path) for it, with an explicit override parameter; a stored
  `alpha_format` on the entity records what was read.

## Staging (incremental commits to main, each compiling + tested)

1. WotLK monolithic: boundaries, wire structs, MapChunk (terrain sub-chunks),
   MCLQ + MH2O structured, ADT<V> monolithic read/write, alpha decode; tests
   vs 3.3.5a. Delivers a working WotLK ADT.
2. Cata+ split files: tex/obj routing, MDID/MHID, MCRD/MCRW, MTXP/MAMP/MCMT;
   tests vs 9.2.7.
3. Legion+ _lod + blend meshes + SL MWDR/MWDS/MTCG/MLDB.
4. Bindings (facade/opaque/stubs).
5. Docs (adt_reference_config, guide/maps.md terrain walkthrough).

Survey harness + JSONs live in the scratchpad (untracked), like m2_survey.py.

## Stage 1 landed (2026-07-26): WotLK monolithic

Files (`formats/adt/`): boundaries.hpp, alpha_codec.hpp, chunks/{header,texture,
liquid,object}.hpp, liquid.hpp (MH2OData/MCLQData), map_chunk.hpp (MapChunk<V>),
adt.hpp (ADT<V> + ADTBase + fs read/write). Key realized designs:
- **MapChunk is NOT a generic ChunkedFile**: a bespoke `read_slice(payload,
  FileKind, AlphaFormat)` / `write_slice` pair. Version-gated members via slot<>
  traits (MapChunkColor since WotLK, MapChunkCata since Cata, MapChunkLegacyLiquid
  until WotLK). MCNK-header offsets stamped locally on write.
- **Derived fields normalized to 0 on read** (SMChunk offsets/sizes/counts, MHDR
  offsets) AFTER they are consumed — else the semantic diff compares layout
  artifacts (my canonical layout ≠ Blizzard's). This is the ADT analogue of M2
  not exposing offsets at all. do_not_fix_alpha is normalized to SET (we hold
  full 64x64 maps) by `normalize_cells()` after all a tile's slices are read —
  NOT inside read_slice, because the tex slice's alpha decode still needs the
  ORIGINAL flag while other slices load.
- **MCSH is independent of MCLY** (a cell can shadow with no texture layers) —
  emit them separately in write_slice (caught by the round-trip).
- **Alpha 4-bit is lossy** (nibble quantization): synthetic exactness tests must
  use 8-bit or pre-quantized values.
- ADT assembly: monolithic (V<Cata) read/write done; MCIN + MHDR offsets stamped
  in a final patch pass over the buffer; AlphaFormat resolved by a light raw MPHD
  scan of the sibling WDT (`resolve_alpha_format`). Split-file read/write return
  NotImplemented (stage 2).
- Semantic-diff oracle: reflective `diff_value` over members_of (flattens trait
  bases; walks StringBlock/liquids), memcmp at leaves (NaN heights). Tests:
  test_adt_wire_layout.cpp (layout asserts + codec + synthetic buffer round-trip),
  test_adt_roundtrip.cpp (3.3.5a real-corpus semantic round-trip). Suite 100/100.
- GOTCHA: `detail::` is ambiguous in tests (wowlib::detail vs formats::detail vs
  adt::detail) — qualify fully.

## Stage 2 landed (2026-07-26): Cata+ split files

- ADT<V> gained `ADTSplit` (Cata+: mamp u32 [MAMP is 4 bytes, not wowdev's 1],
  uses_texture_fdids, texture_params[MTXP], + `obj1_data`/`lod_data` raw byte
  blobs) and `ADTTexFdids` (8.1+: diffuse/height_texture_ids [MDID/MHID]) traits.
  `textures` (MTEX) moved to an always-present own member.
- **MTEX vs MDID/MHID is a per-MAP choice, NOT version-gated** (survey: kultiras
  ships empty MTEX in 9.2.7). Tracked by `uses_texture_fdids` (set when MDID is
  read); write emits the scheme that was read.
- Satellites located by the "{stem}_tex0.adt" naming convention (the 9.2.7
  listfile resolves those paths — no WDT MAID coupling). read() parses
  root→tex0→obj0 MERGING into the one entity (each file's 256 MCNK stream
  accumulates per cell via read_slice's FileKind); _obj1/_lod preserved verbatim
  as raw blobs (structured in stage 3). write() re-emits each file;
  `write_file(kind)` routes chunks per file.
- **`offset_in_mcal` (SMLayer) is derived** → normalized to 0 on read after MCAL
  decode, like the header offsets (else the semantic diff compares layout).
- **MCAL presence differs by era**: pre-Cata writes MCAL in EVERY MCNK (survey:
  1.47M/1.47M), Cata+ omits it when empty (800/3840). Resolved by sizing
  `alpha_maps` to `layers.size()` in the MCLY branch (independent of MCAL
  presence) + emitting MCAL only when the alpha blob is non-empty — both eras
  round-trip. Same principle: empty vs absent chunk both decode to empty vectors,
  so only size-carrying members needed care.
- Tests: 9.2.7 split semantic round-trip (buffer-level per physical file);
  suite 101/101. Deferred: structured _obj1 (MLMD/MLMX/MLDD/MLDX/MLFD) + _lod
  (MLHD/MLVH/MLLL/MLND/…) + blend meshes + SL MWDR/MWDS/MTCG/MLDB;
  MCBB/MCDD/MPTX per-cell.

## Bindings landed (2026-07-26)

Full Python surface, mirroring WDT. Files: instantiations/adt_{ranges.hpp,
matrix.inl,.hpp,.cpp} (ADT 5 ranges Vanilla/Tbc/Wotlk/CataToLegion/BfaPlus;
MapChunk 3 ranges VanillaToTbc/Wotlk/CataPlus — NOT ChunkedFile, so the matrix
just instantiates the entities, no serializer-base row), formats/adt.{hpp,cpp}
(facade: for_version on ADT + MapChunk, read/write/convert on ADTBase, AnyADT/
AnyMapChunk). Wired into wowlib.hpp (adt namespace + chunks, after wdl),
wowlib_module.cpp, opaque_gen.cpp (MANDATORY include), bindings/CMakeLists.txt
(sources + stub outputs adt/{__init__,chunks}.pyi), stub_patterns.nb (AnyADT/
AnyMapChunk). Added MapChunkBase (welded, weld_as "MapChunk") so MapChunk gets
for_version/AnyMapChunk like the other families. Tests: tests/python/
test_adt_facade.py (9 cases incl. the version-agnostic "add a texture"); full
python suite 127/127.
- GOTCHA: a version-trait struct's `operator==` gets FLATTENED onto the welded
  entity binding, and welder tries to bind its parameter (the unwelded trait
  type) → `assert_bindable` failure. Mark every trait `operator==` with
  `[[=welder::mark::exclude]]` (like ChunkExtras/absent). Applies to
  MapChunkColor/Cata/LegacyLiquid and ADTSplit/ADTTexFdids.
- GOTCHA: `fs.add_file` returns `Result<FileDataID>`, not `Result<void>` —
  wrap it (discard the id) before returning from a `Result<void>` writer.

## CORRECTNESS FIX (2026-07-26): MCSH size

`size_shadow` (MCNK header 0x30) is the RAW MCSH data size (512), and does NOT
include the 8-byte chunk header — UNLIKE size_alpha/size_liquid, which do.
Subtracting 8 for MCSH misaligned the sub-chunk stream by 8 bytes, silently
misparsing every shadow-bearing cell (and crashing when the misalignment
overran, e.g. Azeroth_37_23). The semantic round-trip DID NOT catch it — both
sides misparsed identically. Fixed: MCSH trusts its own (reliable) declared
size, no correction. LESSON: a semantic round-trip can't catch a consistent
misparse — the test now also asserts per-cell STRUCTURAL invariants (heights/
normals 0-or-145, shadow 0-or-4096, alpha_maps aligned to layers) over 30
tiles/map, and a full-corpus Python read sweep (both clients, 0 failures)
backs it.

## REFACTOR (2026-07-27): codecs, split-file naming, external alpha, MCLQ

A readability + correctness sweep (both corpus round-trips + synthetic + codec
tests stay green). See [[cpp-conventions]] for the house rules it established.

- **Codec classes** (`codec.hpp`, was `alpha_codec.hpp`): the free
  `decode_alpha_*`/`encode_*`/`decode_shadow`/`fix_last_row_col` functions became
  `AlphaMapCodec` (state = the tile AlphaFormat; `decode(src, compressed, fix)` /
  `encode(map, compressed, out)`; per-encoding routines protected) and
  `ShadowMapCodec`, sharing a `TerrainMapCodec` base for `fix_last_row_col`. No
  free functions for domain operations.
- **Terminology (canonical WoW modding, per the user)**: ADT = "tile", MapChunk =
  "chunk", the Cata+ satellites = "split ADT files". `MapChunk::read_slice/
  write_slice` → **`read_from`/`write_to(FileKind)`** (renamed again in the second
  pass — "split" awkwardly included the monolithic case); `slice_*` predicates →
  **`file_has_header`** + the shared **`routes_to(InFile, FileKind)`**;
  **`ADT::cells` → `ADT::chunks`** (a Python-visible rename — bindings pick it up
  by reflection off the member identifier, no binding-code change; docs/tests
  updated). `MH2OData::cells` (liquid-per-chunk) kept its name.
- **MapChunk read_from/write_to are ALSO reflection-driven** (2026-07-28, third
  pass — "can MapChunk get the same treatment?"). Sub-chunk members carry the same
  `chunk("MCVT")` + `in_file(InFile::root|tex|obj)` annotations as the tile chunks;
  `InFile`/`in_file`/`routes_to`/`FileKind` live in map_chunk.hpp and are SHARED by
  both levels (the monolithic file = "routes to every group", exactly `routes_to(_,
  monolithic)`). read_from is a `template for` over the members matching fourcc.
  The UNIFORM sub-chunks (MCVT/MCCV/MCLV/MCSE/MCRD/MCRW/MCMT, and MCLQ once
  `MCLQData::read` was made tolerant of the empty ≤8-byte form → SelfSerializing)
  transfer through the engine's `read_value`/`write_value`. The ones that break the
  plain mapping carry a **`serialized_by(^^Codec)`** annotation (a `serializer_spec`
  holding a `std::meta::info`; the loop splices `using Codec = [:ser->codec:]`)
  naming a MapChunk-context codec struct with static `read(self, span, ReadCtx)` /
  `write(self, out, WriteCtx)` / `engaged(self, WriteCtx)` templates:
  `NormalCodec` (MCNR + 13 padding), `LayerCodec` (MCLY + sizes alpha_maps),
  `AlphaCodec` (MCAL codec, indexes sibling layers via the ctx), `ShadowCodec`
  (MCSH codec). The context carries the header, sibling `layers`, `af`, `fix` — a
  MapChunk-local "SelfSerializing WITH context" (the plain ChunkedFile
  SelfSerializing can't reach siblings or the header). **MCRF is the one
  hand-written special** (one chunk → doodad_refs + object_refs, and it changes to
  MCRD/MCRW on split) — the MCNK analogue of ADT's MCNK. write_to walks a canonical
  `sub_chunk_order` (refs excluded, emitted last via `_write_refs`); header offset
  stamping stays CENTRAL and faithful (`_stamp_header` over an `Emitted[]` table —
  NOT distributed to the codecs, because those offset values are derived and the
  semantic round-trip can't catch a wrong one). GOTCHAs: same `[:m:]`-splice-outside-
  the-lambda rule as ADT; `AlphaCodec::prepare` pre-builds the blob + stamped layers
  BEFORE the write walk because MCLY (emitted first) needs the offset_in_mcal MCAL
  computes; the `serialized_by`/`in_file` annotations coexist with welder's opaque-
  vector binding on the same members (full suite green: 101 C++, 127 py).
- **Alpha format is EXTERNAL, never resolved (user item)**: `resolve_alpha_format`
  (the sibling-WDT MPHD scan) is DELETED. `ADT::read(fs, key, AlphaFormat)` and
  `ADT::write(fs, key, AlphaFormat)` take it explicitly; `write_monolithic(af)` /
  `write_split_file(kind, af)` thread it. The `alpha_format` member is kept only as
  a record of what read() was told. Callers resolve it themselves from the map's
  WDT MPHD (adt_has_big_alpha 0x4 / adt_has_height_texturing 0x80) — the round-trip
  tests read the WDTRoot they already load and pass `alpha_format_of(root.header.flags)`.
- **MCLQ available THROUGH WotLK (user correction)**: `MapChunkLegacyLiquid` was
  gated `until WotLK` (removed AT wotlk) — WRONG. Outland (Expansion01) tiles in a
  3.3.5a client DO ship MCLQ, and the exact removal build is unknown, so it is now
  `until Cata`. This exposed a latent DATA-LOSS: the old WotLK MapChunk had no
  legacy_liquid member at all, so it silently DROPPED every Outland MCLQ (the
  semantic round-trip passed because both sides dropped it — the MCSH lesson
  again). Now read. GOTCHA: an "empty" MCLQ (MCNK header size_liquid <= 8, payload
  < 724 = the fixed-record minimum) means no liquid — `_read_legacy_liquid` skips
  it rather than erroring (Expansion01_44_7 has these).
- **parse_file / write_file are REFLECTION-DRIVEN** (2026-07-27, second pass — the
  user: "mark fourcc on containers as annotations"). Every tile-level chunk member
  carries `chunk("XXXX")` (+ `in_file(InFile::root|tex|obj)`, + `formats::optional`
  for conditional ones) — the same `annotations.hpp` vocabulary WMO uses. parse_file
  is a `template for` over `members_of<ADT<V>>()` matching fourcc → `read_value` into
  the member (reusing the chunked_file.hpp engine's per-member transfer); MCNK is the
  only hand-written case (256 repeats via read_from), and two members get a post-read
  fix-up (MHDR offset-zeroing, MDID sets uses_texture_fdids). The two writers
  (write_monolithic/write_split_file) COLLAPSED into ONE `write_file(kind, af)`: it
  walks a canonical `static constexpr chunk_order` fourcc array and, per entry,
  reflectively emits the annotated member whose magic matches IF `routes_to(in_file,
  kind)` and `_write_engaged<m>()` (required always; `optional` when non-empty; MFBO
  tracks the header flag; MTEX/MDID/MHID encode the name-vs-FileDataID scheme). MCNK,
  MCIN, and the derived MHDR/MCIN offset stamping stay explicit. GOTCHA: a `[:m:]`
  splice must sit in the `template for` body, NOT inside a lambda passed to
  `_emit_chunk` — captured by reference, m stops being a constant expression
  ("call to consteval … not a constant expression"); bind `const auto& member =
  this->[:m:];` first. The `chunk()`/`in_file`/`optional` annotations coexist with
  welder on the same welded members (welder ignores non-welder annotations) — full
  suite stays green (101 C++, 127 py). NOTE the canonical `chunk_order` reproduces
  every real per-file layout by SUBSETTING: monolithic emits all groups, each split
  file only its `in_file` group, in the one order (survey-verified).

## Why ADT is NOT a ChunkedFile (item 8 answer, engine study 2026-07-27)

The user asked why ADT doesn't use the annotation-driven `ChunkedFile` engine
(chunked_file.hpp) that WMO/WDT/WDL do. A full study of the engine confirmed it
CANNOT drive ADT — four hard limits, each of which ADT hits:
1. **No external-field-governed payload lengths.** Every engine path derives a
   chunk's length from its own `size` field / `payload.size() / sizeof(T)`. ADT's
   MCAL/MCLQ lengths come from the MCNK header (size_alpha/size_liquid), and MCNR
   has 13 undeclared trailing bytes — no engine hook consults another field.
2. **Fixed single-span `read(span)` signature.** MCNK needs
   `read_from(payload, FileKind, AlphaFormat)` — extra context args the engine's
   `SelfSerializing`/`read_value` contract cannot pass.
3. **No accumulate/merge across files.** `read_entity` starts fresh and overwrites
   member-by-member; ADT parses root → _tex0 → _obj0 MERGING each file's 256-MCNK
   stream into one entity per chunk. No engine analogue.
4. **Single output buffer only.** `write_entity` appends to one FileBuffer; there
   is no fourcc→physical-file routing for the split-file writer.
The tile-level chunks that ARE engine-expressible (MVER/MHDR structs, MDID/MDDF/
MODF/MTXP vectors, MTEX/MMDX string blocks, MH2O SelfSerializing) don't justify
adopting the whole engine — but they DO carry the engine's `chunk()` annotations
and parse_file/write_file reflect over them and reuse `read_value`/`write_value`
(see the reflection-driven note above). So ADT is a bespoke reader/writer at the
MCNK/split-routing/offset-stamping level, annotation-driven for everything else.

## Bindings build blocker (pre-existing, NOT ADT — 2026-07-27)

The Python `.so` build (`build/bindings`) is broken independent of ADT: welder's
nanobind rod (`rod.hpp:187/190`) tries to take the address of
`M2OffsetBlock::member_offset`, which is `consteval` (an immediate function) —
`error: taking address of an immediate function`. Present on the Jul 26 tree,
before this refactor; the on-disk `.so` is stale. The ADT binding TUs
(`bindings/python/formats/adt.cpp`, `instantiations/adt.cpp`) compile clean; only
the whole-module LINK fails on M2. So the ADT Python facade test could not be run
this session — the C++ suite (incl. both corpus round-trips) is the coverage.
Fixing it needs excluding the static consteval `member_offset` from the welder
walk (formats-architecture says static members should be invisible to welder — a
welder-pin regression), which is M2/welder scope, left for the user.
