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
