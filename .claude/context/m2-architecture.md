# M2 subsystem architecture (plan of record)

Decisions fixed with the user 2026-07-24, before any code. M2 support lands as
INCREMENTAL COMMITS TO MAIN, each stage compiling + tested:

1. **Stage 1 — offset engine**: `formats/common/offset_serializer.hpp` (the
   `offset_view` generalization promised in formats-architecture.md) + synthetic
   unit tests (`tests/unit/test_offset_framework.cpp`).
2. **Stage 2 — MD20 body**: `formats/m2/` — `M2Data<V>` (client symbol name) for
   all 11 versions, record structs, `Skin<V>` (.skin + pre-WotLK embedded
   profiles), .anim baking, `M2<V>` assembly with fs read/write for pre-Legion;
   integration round-trip vs local 3.3.5a client.
3. **Stage 3 — Legion+**: chunked outer file (MD21 + PFID/SFID/AFID/BFID/TXID/
   SKID/…, **forward fourccs**), `Skeleton` entity (SKL1/SKA1/SKB1/SKS1/SKPD),
   `.bone` entity (BIDA/BOMT + raw u32 prelude), .phys as opaque baked blob;
   integration vs local 9.2.7 client.
4. **Stage 4 — bindings + docs + convert scaffolding** (mirror the WMO recipe).

## User decisions (do not relitigate)

- **NO byte-perfect round-trip for M2** (unlike WMO). Canonical relayout on
  every write; the guarantee is *valid + loads back*: tests compare
  `parse(write(parse(x))) == parse(x)` (semantic equality via defaulted
  `operator==`), not memcmp. Later refinement may align the writer with
  Blizzard's exporter by diffing real files — design the writer so layout
  policy is one replaceable function, but do NOT build a layout journal.
- **All 11 versions** modeled from the start (vanilla → tww), tests against
  3.3.5a + 9.2.7 like WMO.
- **.phys stays an opaque blob** this milestone (bytes baked into the M2
  entity, PFID/inline-PFDC round-tripped verbatim). Structured PHYS is a
  follow-up — note its records are gated by the PHYS version field *inside*
  the file (v0–6), a runtime axis our NTTP ClientVersion doesn't cover.
- External files (.skin/.anim/.skel/.bone/.phys) are BAKED into the loaded M2
  entity; the serializer re-splits on write from flags/links (sequence flags
  `(flags & 0x130) == 0` → .anim, SKID → .skel, SFID/names → .skin,
  global_flags 0x20 → .phys). `Skeleton` is a FIRST-CLASS entity with its own
  read/write — .skel is shared between models (SKPD parent link, e.g.
  lightforgeddraeneimale → draeneimale_hd), so an M2 references, never owns.

## Format facts driving the design (wowdev, fetched 2026-07-24)

- MD20 body is NOT chunked: header of `M2Array{u32 count, u32 offset}` fields,
  offsets relative to MD20 start (== MD21 chunk data start in Legion+ files).
  Legion+ outer file is chunked with **non-reversed** fourccs
  (`chunk("PFID", FourCCEndian::forward)`); pre-Legion files may still be raw
  MD20 even in Legion+ clients — dispatch on the leading magic when reading.
- Tracks: `M2Track<T>` = u16 interpolation, u16 global_sequence, then
  **vanilla/TBC (v256–263)**: `M2Array<M2Range> ranges + M2Array<u32>
  timestamps + M2Array<T> values` (single global timeline);
  **WotLK+ (v264+)**: `M2Array<M2Array<u32>> timestamps + M2Array<M2Array<T>>
  values` (one inner array per sequence). Per-sequence inner arrays of
  sequences stored externally point into the matching .anim file
  (`%s%04d-%02d.anim` by anim id/sub id, or AFID fdids), offsets relative to
  that file. Alias sequences (flag 0x40) follow aliasNext and own no data.
- M2 wire version per our grid: vanilla 256, tbc 263, wotlk 264,
  cata/mop/wod 272, legion+ 274. Stored in-entity as `format_version` (the
  name `version` is taken by the ClientVersion static).
- Pre-WotLK: skin profiles EMBEDDED (`M2Array<M2SkinProfile>` in the header),
  plus playable_animation_lookup + texture_flipbooks members; WotLK+ replaces
  the profile array with `u32 num_skin_profiles` and external .skin files
  (magic 'SKIN'; embedded profiles have no magic → gate by since(wotlk)).
- Record layout pivots (constrained partial specializations, WMO-batch style):
  M2Sequence (≤TBC start/end timestamps vs duration; WoD+ splits u32 blendTime
  into blendTimeIn/Out), M2CompBone (vanilla C4Quaternion vs M2CompQuat;
  TBC+ boneNameCRC union), M2Particle (Cata+ 492-byte layout: multitex union +
  trailing multiTexScrollMid/Range; ≥262 blendingType/emitterType u8+u8+
  particleColorIndex vs u16+u16 — our grid: vanilla=old, tbc+=new),
  M2Camera (Cata+ drops static fov, adds FoV spline track), M2Ribbon (WotLK+
  trailing priorityPlane/colorIndex/textureTransformLookup). FBlock (the
  fake-animation block, particle color/scale/UV) has NO per-sequence layer in
  any version.
- textureCombinerCombos: trailing header M2Array present iff
  `global_flags & 0x8` — the one flag-gated wire member (`gated_by(0x8)`).
- .skel chunks SKL1/SKA1/SKB1/SKS1 are headers-with-M2Arrays whose offsets are
  relative to the CHUNK data; SKPD carries parent_skel_file_id; AFID/BFID as
  in M2. A skel-based model's .anim files are chunked (AFM2 + AFSA attach /
  AFSB bone splits); non-skel new-format .anim = AFM2 wrapping the classic
  blob; classic .anim = raw blob, bit-identical content.

## Offset-engine design (stage 1)

Sibling of the chunk serializer, same philosophy: annotations + `template for`
over reflected members; member kinds are TYPE-DRIVEN. In-memory entities store
`std::vector<T>`/`std::string`; the wire `M2Array` never surfaces as a user
type. Wire layout of a record = members walked in canonical order, cursor
advanced by consteval `wire_size<T>()`: trivially-copyable → `sizeof(T)`,
vector/string → 8 (M2Array), non-trivial struct → recursive member sum (client
records have no implicit padding). Element kinds recurse, so
`vector<vector<u32>>` (per-sequence tracks) and `vector<Record>` (records
containing nested arrays) fall out naturally.

- `OffsetFile<Derived>` CRTP mixin: `read(span)`, `write() -> FileBuffer`,
  plus `write(FileBuffer&)`/`empty()` so an offset entity satisfies
  **SelfSerializing** and can sit directly as a chunk member (MD21 in the
  Legion wrapper, SKB1 etc. in .skel).
- **Versioning**: entity-level interleaved diffs use conditional trait bases
  (version_slot) + an authoritative `static constexpr wire_order` — an array
  of member NAMES (identifier_of match, offset entities have no fourccs) that
  fixes the wire walk order across flattened traits; consteval-checked to list
  every serialized member exactly once. Record-level TYPE changes in place =
  constrained partial specializations. since/until survive as doc metadata +
  active-member filter.
- **Write = canonical relayout**: reserve the entity's wire image, then
  depth-first in wire order: per vector, one element-image block (16-byte
  aligned, zero gap fill — Blizzard-preferred), then each element's nested
  blocks; M2Array slots backpatched. Empty vector → {0, 0}. Strings write
  size = length + 1 (NUL included — client requires the terminator counted).
- **External-data hooks built in from day one** (retrofit would be costly):
  `sequence_data` annotation on the per-sequence nested members; read/write
  contexts resolve outer index i → base span (read) / target buffer (write),
  defaulting to the entity's own. Stage 2 wires .anim through it.
- New ErrorCode: `OffsetOutOfBounds` (an M2Array points outside the buffer).

## Naming

Client canonical record names (M2Sequence, M2CompBone, M2Vertex, M2Batch,
M2SkinSection, …); entities `M2<V>` (assembly), `M2Data<V>` (MD20 body — the
client's own symbol), `Skin<V>`, `Skeleton`, `BoneFile`. Directory mirrors
namespace: `formats/m2/{body/…,skin/,skel/,anim/,bone/}` with per-family
record headers under `body/records/`.
