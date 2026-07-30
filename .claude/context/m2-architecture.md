# M2 subsystem architecture (plan of record)

## Conventions (ACTIVE — read before editing M2 code)

- **Non-public methods carry a leading underscore** (user request 2026-07-27,
  "for now"): every `private:` method of `detail::M2<V>` (m2.hpp) and every
  `protected:` engine method of `M2OffsetBlock` (offset_block.hpp) is named
  `_name` (e.g. `_read_chunked`, `_write_members`, `_layout` helpers). PUBLIC
  API stays unprefixed (`read`/`write`/`image_size`/`member_offset`/`empty`,
  the free `layout_size`, the concepts). When adding a new internal helper to
  either class, give it the `_` prefix. This is M2-only for now — the chunk
  framework and other formats do NOT use it. (Leading-underscore + lowercase
  inside a class/namespace is not a reserved identifier; only `_Upper` and
  `__` are.)

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
- M2 binary version per our grid: vanilla 256, tbc 263, wotlk 264,
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
  `global_flags & 0x8` — the one flag-gated binary member (`gated_by(0x8)`).
- .skel chunks SKL1/SKA1/SKB1/SKS1 are headers-with-M2Arrays whose offsets are
  relative to the CHUNK data; SKPD carries parent_skel_file_id; AFID/BFID as
  in M2. A skel-based model's .anim files are chunked (AFM2 + AFSA attach /
  AFSB bone splits); non-skel new-format .anim = AFM2 wrapping the classic
  blob; classic .anim = raw blob, bit-identical content.

## Offset-engine design (stage 1)

Sibling of the chunk serializer, same philosophy: annotations + `template for`
over reflected members; member kinds are TYPE-DRIVEN. In-memory entities store
`std::vector<T>`/`std::string`; the binary `M2Array` never surfaces as a user
type. Binary layout of a record = members walked in canonical order, cursor
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
  fixes the binary walk order across flattened traits; consteval-checked to list
  every serialized member exactly once. Record-level TYPE changes in place =
  constrained partial specializations. since/until survive as doc metadata +
  active-member filter.
- **Write = canonical relayout**: reserve the entity's binary image, then
  depth-first in binary order: per vector, one element-image block (16-byte
  aligned, zero gap fill — Blizzard-preferred), then each element's nested
  blocks; M2Array slots backpatched. Empty vector → {0, 0}. Strings write
  size = length + 1 (NUL included — client requires the terminator counted).
- **External-data hooks built in from day one** (retrofit would be costly):
  `sequence_data` annotation on the per-sequence nested members; read/write
  contexts resolve outer index i → base span (read) / target buffer (write),
  defaulting to the entity's own. Stage 2 wires .anim through it.
- New ErrorCode: `OffsetOutOfBounds` (an M2Array points outside the buffer).

## Stage 1+2 landed (2026-07-24)

- Stage 1 commit 77a29ba: engine as designed above. Stage 2: `formats/m2/`
  complete for pre-Legion — boundaries.hpp (+ `m2_wire_version_range`: read
  accepts the whole era, e.g. tbc 258–263), records/{track,sequence,bone,
  material,scene,effects,skin}.hpp, data.hpp (M2Data + trait slots
  DataPreWotlk<V>/DataWotlk/DataTbc + 41-name wire_order), skin.hpp (Skin<V>
  = magic + inherited M2SkinProfile<V>, wire_order puts magic first),
  m2.hpp/.cpp (assembly + X-macros ×11 + skin-only X-macro ×9), convert.hpp.
  All binary sizes static_asserted against wowdev (vanilla header 324, wotlk
  304, bone 108/112/88, particle 504/476/492, camera 124/100/116, ribbon
  220/176, skin section 32/48).
- **Engine addition**: a `sequence_data` member's per-sequence resolution is
  SKIPPED when the parent record's `global_sequence != 0xFFFF` — global-
  sequence tracks keep their single timeline inline even when sequences are
  external. Both read and write gate on it.
- **Assembly I/O** (m2.cpp): read resolves .anim lazily inside the context
  closure (sequences decode before any track member — binary order — so flags
  are available); missing .anim leaves tracks empty, and write regenerates an
  empty file for them. Aliases (0x40) never own a file. Skins are
  "{stem}0N.skin"; write validates num_skin_profiles == skins.size()
  (ErrorCode::InvalidEntityState). Legion+ read/write are NotImplemented
  until stage 3.
- **NaN gotcha**: real client data holds NaN floats, so defaulted
  operator== is NOT the round-trip test oracle — the integration test
  (test_m2_roundtrip.cpp) uses a reflection-driven bitwise diff_value()
  (memcmp leaves, member-path output) instead. Keep that pattern for the
  9.2.7 tests.
- Integration: 11 curated 3.3.5a models (chicken → HumanMale → KelThuzad →
  the Northrend glue screen) re-read bit-identically after canonical rewrite,
  .anim splitting included. Full suite 85/85.
- Deferred in-stage: span-based M2::read overload (bindings will need it),
  welder welds on templated records (stage 4), real client write-through-fs
  test (needs a project-overlay fixture).

## Stage 3 landed (2026-07-24)

- **3a — chunked shell** (commit 0ead6a5): `chunked.hpp` M2File<V> (Legion+),
  FORWARD fourccs. MD21 = ChunkBlob at shell level (assembly decodes);
  typed: PFID/SKID as 0-or-1 vectors (presence policy), SFID/AFID/BFID/TXID/
  RPID/GPID vectors, EXPT/TXAC/DETL record vectors, EXP2/PABC/PSBC/PGD1 as
  offset-entity payloads (they shadow OffsetFile::empty() for engagement);
  blobs: LDV1/PADC/PEDC/WFV1-3/PFDC/EDGF/NERF/DBOC/AFRA/PCOL/DPIV. The shell
  is byte-perfect on plain round-trip EXCEPT when the typed offset-entity
  chunks are engaged (their payloads re-encode canonically) — the 9.2.7 test
  branches on that.
- **3b — Skeleton/.bone** (this commit): `records/skel.hpp` (SkelHeader/
  SkelSequences typed chunk payloads, SkelBones/SkelAttachments decoded from
  SKB1/SKA1 blobs, SkelParentData), `skel.hpp/.cpp` Skeleton<V> — first-class
  read/write(fs,key); read follows the SKPD parent ONE hop for shared
  AFID/BFID (child keeps own SK*1), decodes bone/attachment blocks through
  AnimCache windows (AFSB/AFSA); blobs kept after decode so chunk-level
  write of an untouched skel stays byte-perfect; standalone write re-encodes
  + emits AFSA/AFSB-only .anim files (documented: the paired M2's write is
  the full-fidelity path restoring AFM2). `bone_file.hpp` BoneFile
  (u32 prelude via `header` annotation + BIDA/BOMT; not version-templated).
  `satellite_io.hpp` (m2::detail): naming helpers + AnimCache (lazy loader,
  per-fourcc chunk windows, raw-file AFM2 fallback) + append_chunk.
- **Assembly**: AssemblyLegion slot = shell + lod_skins + phys(ChunkBlob) +
  skel + bone_files. Read: skel loads FIRST, then the body decodes with the
  SKELETON's sequences gating externals (body's own table is empty on skel
  models). Write: bone files → .anim files (AFM2+AFSA+AFSB assembled per
  sequence for skel models; AFM2-or-raw per global_flags 0x2000 otherwise)
  → skel (fdids hung off it) → skins/lods → phys → shell (fresh fdids in
  the reference chunks).
- Integration: 30 9.2.7 models incl. lightforgeddraeneimale (the parent-skel
  child — hardest case: SKPD + shared satellites) — shell round-trip + body
  and SK*1 blocks re-read bit-equal. In the WoWCircle repack the base
  character models served are non-skel; only lightforged exercises 3b, keep
  an eye out on a retail 9.2.7+ install later. Suite 86/86.

## Stage 4a landed (2026-07-24): Python bindings

The whole M2 surface welds (~350 classes; see bindings-notes.md "Gotchas
learned (M2 facade)" for every rule this taught: instantiable trait
primaries, excluded base operator==, payload OffsetFile explicit
instantiation, subset-family facade guards, opaque_extra removal). Key
shapes:
- Records welded as CONCRETE per-version classes (no family bases /
  for_version on records — construct `records.M2CompBoneWotlk()` directly);
  aliases in m2.hpp X-macros ordered tracks → records → satellites →
  assembly (welded NSDMI defaults convert eagerly at registration).
- Member docs converted /**< */ → welder::doc across records (welded-entity
  doc policy); class doxygen folded into the weld doc() and removed.
- Skin<V> switched from profile INHERITANCE to a `profile` MEMBER (identical
  binary layout — inline record): a welded M2SkinProfile base would have been
  a second welded base beside SkinBase (nanobind allows one).
- Facade: for_version/isinstance/AnyX on M2/M2Data/Skin/M2File/Skeleton
  bases; read/write(fs,key) + convert on M2Base; read/write on SkeletonBase;
  stub_patterns.nb grew the five AnyM2* entries; stubs OUTPUT +=
  formats/m2/{__init__,records}.pyi.
- tests/python/test_m2_facade.py (13 cases: subset eras, unions, version-
  gated members, synthetic byte round-trip through Python) + typing/
  test_m2_facade.mypy-testing (narrowing). 88/88 with clients.
- GOTCHA: the .venv editable install (scikit-build meta-finder,
  _editable_skbc_wowlib.pth) SHADOWS PYTHONPATH — refresh site-packages'
  wowlib.abi3.so + wowlib-stubs/ from build/bindings after rebuilding, or
  imports test the stale module.

## Post-survey policy: alias sequences (2026-07-24, commit 3f97806)

Alias sequences (flags 0x40) own NO track data — the client follows
alias_next and never parses their animation blocks — yet files carry STALE
per-sequence array records for them (dead offsets; HumanMale's aliases
decode to denormals + NaN, and out-of-bounds records would abort reads).
wowlib therefore NEVER chases them: every read context returns the empty
span for alias indices (arrays decode empty), writes emit {0, 0}. Rule
centralized as detail::sequence_is_alias (satellite_io.hpp). This is also
why byte-level comparison, not operator==, is the round-trip oracle
(NaN != NaN): see test_m2_roundtrip's diff_value and the survey script's
write→read→write byte-stability cycle. The full-corpus survey (temp
m2_survey.py in the project root, untracked): 91,705/91,705 served 9.2.7
models clean incl. round-trip; DETL's real records are 16 bytes vs
wowdev's 12 (kept verbatim, ec7c7c6); M2SkinSection.level extends only
index_start.

## Refactor sweep (2026-07-24 evening; user's 8-point request)

- **Directory = namespace = Python package** (user chose the full WMO-style
  mirror over a files-only regroup): `formats/m2/` root keeps the two
  top-level file entities — m2.hpp (M2 assembly) and skeleton.hpp (Skeleton,
  renamed from skel.*, its SKL1/SKS1/SKB1/SKA1/SKPD payload records inlined,
  welded at formats.m2 level) — plus boundaries/convert/io.hpp. Sub-entities:
  `body/` (m2::body: data.hpp M2Data, shell.hpp M2File — was chunked.hpp;
  `body/records/` m2::body::records with shell.hpp = old companion.hpp),
  `skin/` (m2::skin: skin.hpp Skin + records.hpp profiles/sections/batches),
  `bone/` (m2::bone: bone.hpp BoneFile), `detail/satellite_io.hpp`. m2.hpp
  hoists (`using body::M2Data; using skin::Skin; ...`) so C++ spells short
  names; Python mirrors: formats.m2.body.M2DataWotlk,
  formats.m2.body.records.*, formats.m2.skin.SkinWotlk, formats.m2.bone.
- **No explicit instantiations in the library** (also WMO): fs-level
  read/write definitions moved from m2.cpp/skeleton.cpp into
  formats/m2/io.hpp (helpers now documented m2::detail templates); consumers
  implicitly instantiate only the versions they use. The full matrix lives in
  bindings/python/instantiations/{m2,wmo}.{hpp,cpp} (extern header included
  by every binding TU + one instantiation TU per format). GOTCHA:
  `template for` ranges inside a consteval fn must be `static constexpr`
  (wire_offset_of failed constant evaluation only for versions no test
  instantiated — the bindings matrix caught it).
- **Derived counts**: num_skin_profiles is a HIDDEN binary field
  (mark::exclude) stamped by M2<V>::write from skins.size() via the new
  consteval `wire_offset_of<M2Data<V>>("num_skin_profiles")` patch into the
  written image (the old mismatch error is gone; standalone M2Data write
  emits the stored value). MD20/SKIN magic members are hidden too.
- **Raw-MD20 fallback is Legion-only**: new boundary `m2_chunked_only`
  {8,0,1,0} — BfA+ reads of a bare MD20 magic return FormatVersionMismatch
  with guidance instead of the monolithic fallback (no such files exist 8.0+).
- **Nested track containers bind opaque** (welder 5bf54fa): the opaque
  generator now opens `vector<vector<T>>` chains by reference (see
  bindings-notes).

## Stage 4b: docs site LANDED (2026-07-24); remaining polish

- Docs: DONE — the WMO generator was generalized into
  docs/format_reference_impl.py (engine) + {wmo,m2}_reference_config.py
  (declarative per-format configs; a side has a KIND: chunked with FourCC
  badges vs offset entity with wire_order field listing). M2 gets
  python/m2/fields.md (body taxonomy + shell chunks) through the same badge
  pipeline; per-format wowdev anchor maps refresh via
  `docs/build.py refresh-anchors`. Docs build is warning-clean.
- Still open:
  - span+satellites parse API for buffer-driven Python reads (M2Base.read
    currently fs-only, documented).
  - Record-family bases/for_version if ergonomics ever demand; Lua target
    when reinstated; real convert_step ladders (m2/convert.hpp scaffold
    ready).
  - Deferred cleanup from the 2026-07-24 review: the .anim-file ASSEMBLY
    plumbing (union section-buffer keys → append_chunk → add_file → collect
    AnimFileEntry) is still written ~3x across Skeleton<V>::write and
    M2<V>::write's skel/non-skel branches (io.hpp) — a
    detail::write_anim_files(span<(magic, bufs&)>) helper would collapse it;
    and Skeleton<V>::write's `copy = *this` duplicates bone/attachment
    blocks it never reads (copy only the chunk members).
  - Known cosmetic: nanobind prints exit-time "leaked type/function"
    warnings (CAaBox/C3Vector + ~30 functions) at interpreter shutdown —
    PRE-EXISTING (appears in builds before the 2026-07-24 sweep too),
    likely the eagerly-converted NSDMI default objects; harmless but worth
    a look someday.

## Naming

Client canonical record names (M2Sequence, M2CompBone, M2Vertex, M2Batch,
M2SkinSection, …); entities `M2<V>` (assembly), `M2Data<V>` (MD20 body — the
client's own symbol), `Skin<V>`, `Skeleton`, `BoneFile`. Directory mirrors
namespace (realized 2026-07-24, see Refactor sweep): `formats/m2/{body/…,
skin/,bone/,detail/}` with body record headers under `body/records/` and the
top-level entities (m2.hpp, skeleton.hpp) at the root.

## Second sweep (2026-07-25; user's 5-point request) — CURRENT LAYOUT

Supersedes the naming/layout notes above (kept as history):

- **M2Data → M2Root** (`m2/root/root.hpp`): the entity template is PUBLIC at
  `m2::root::M2Root` — no detail:: nesting (user: "M2Data itself should not
  live in a detail namespace"); the canonicalizing alias sits in m2
  (`m2::M2Root = root::M2Root<canonical>`). Trait slots stay in root::detail,
  qualified `root::detail::` in the base list (bare `detail::` is ambiguous
  against record::detail via the using-directive). Records live in
  `m2::root::record` (`m2/root/record/*.hpp`, singular).
- **M2File → M2ChunkedFile** (`m2/chunked/chunked.hpp`): same shape — public
  template `m2::chunked::M2ChunkedFile`, canonicalizing alias in m2; its
  companion-chunk payload records (AnimFileEntry, EXPT/EXP2, DETL,
  Exp2/Pabc/Psbc/Pgd1Data) in `m2::chunked::record`
  (`m2/chunked/records.hpp`). Python mirrors: formats.m2.root(.record),
  formats.m2.chunked(.record).
- **X-macros live in the bindings now**: every WOWLIB_*_RANGES table, the
  welded per-range aliases and the ranges_valid checks moved from
  m2.hpp/wmo.hpp to `bindings/python/instantiations/{m2,wmo}_ranges.hpp`
  (included by instantiations/{m2,wmo}.hpp). GOTCHA: opaque_gen.cpp MUST
  include both ranges headers — the opaque-container walk finds welded
  per-version entities only through the aliases; without them it emits an
  empty header and every vector silently binds by value (caught by
  tests/python/test_vectors.py failing).
- **satellite_io.hpp → m2/satellites.hpp entities** (m2/detail/ is gone):
  `SequenceKey` (id+variation, keys the buffer/cache maps and the .anim
  naming), `SatellitePaths` (stem + skin/lod_skin/anim/bone/skel/phys
  naming), `AnimCache` (lazy window cache; afm2/afsa/afsb magics and
  afid_lookup as statics; `sequence_base()` member = the shared read
  resolver), `AnimBuffers` (write-side per-sequence buffers: `sink()`,
  `entries()`, `append_chunk_to()`, `merged_keys()`). The alias/external
  predicates became WELDED METHODS on every M2Sequence era variant:
  `is_alias()`, `owns_anim_file()`.
- **Named build constants** (`core/client_builds.hpp`, wowlib::builds):
  every =since()/=until() and pivot-list version spells an
  EXPANSION-PREFIXED patch name — short community expansion names
  (TBC/WotLK/Cata/WoD/Legion/BfA/SL/DF/TWW) as era markers at build 0, and
  `<Expansion>_<PatchTitle>` for exact builds (Legion_Alpha 7.0.1.20740,
  BfA_TidesOfVengeance 8.1.0.27826, TWW_LegacyOfArathor 11.1.7.60520).
  Disambiguate same-patch builds with the build number
  (Legion_ShadowsOfArgus_24473/_24500). User: bare patch titles were too
  hard to place in an expansion; NO long expansion names.
- **No io.hpp anywhere** (user: order-dependent includes, no use case for a
  template head without its defs): the fs-level read/write definitions are
  INLINE at the bottom of m2.hpp, skeleton.hpp and wmo.hpp; convert/parse
  consumers get them along with the entity. formats/m2/io.hpp and
  formats/wmo/io.hpp are gone.

## Third sweep (2026-07-25 follow-up)

- **`wire_order` is GONE** (user: string blobs are hard to maintain). The
  offset serializer's walk (offset_file.hpp `offset_order`) is now: the
  entity's OWN members in declaration order, with each trait-base member
  spliced in right after the own member its `=wire_after("name")` annotation
  names (new spec in annotations.hpp; the name interns via
  `std::define_static_string` — annotations must be structural, string_view
  is not). Consteval-checked both ways (unanchored base member / anchor naming
  no own member). A pure "own members then traits" walk CANNOT work — the
  MD20 header interleaves trait members mid-struct — which is why anchors
  carry the position. M2Root now has 5 anchors instead of the 41-name array;
  declaration order == binary order for the own members (keep it that way).
- **Chunked payload records canonicalized** (chunked/records.hpp): Exp2Data/
  PabcData/PsbcData/Pgd1Data moved to `record::detail` + canonicalizing
  aliases over `m2_chunk_payload_pivots` (RENAMED from m2_skeleton_pivots;
  empty — one range) × m2_chunked_versions, like the Skel* payloads. Before,
  `M2ChunkedFile<V>` dragged an UNWELDED `Exp2Data<V>` per shell version:
  BfA+ shells exposed raw C++ template names in the stubs and would have
  thrown unregistered-type on property access. Rule: any versioned member
  type on a versioned entity must go through a canonicalizing alias.
- **…SUPERSEDED (2026-07-25): those four payloads are now NON-templated.**
  Their content is version-independent (Exp2Data's M2ExtendedParticle holds
  the unversioned M2PartTrack; PSBC holds unversioned M2Bounds; PABC/PGD1 are
  plain u16 arrays) and the chunks only exist Legion+, so any embedded offset
  structure is fixed at its WotLK+ layout. They dropped the `<V>` template and
  fix `static constexpr version = versions::legion`; the shell holds them as
  plain `Exp2Data` (not `Exp2Data<V>`). Removed: the record::detail nesting,
  the canonicalizing aliases, the WOWLIB_M2_SHELL_RECORD_ALIAS block, and the
  per-range OffsetFile instantiations (now one each, after the RANGES loop in
  m2_matrix.inl). Welds as plain `Exp2Data`/etc. — one class, cleaner docs (no
  LegionPlus suffix). The Skel* payloads STAY templated (SkelSequences/
  SkelBones/SkelAttachments genuinely embed M2Sequence<V>/M2CompBone<V>/
  M2Attachment<V>). Refined rule: propagate V only when the content actually
  varies with it; a Legion+-only payload of version-independent members fixes
  the version instead.

## Offset-serializer readability sweep (2026-07-27; user's 5-point request)

A pure readability pass on the offset serializer — behavior-preserving,
verified byte-for-byte (synthetic 85, real 3.3.5a 141, real 9.2.7 356
assertions, all unchanged; the m2.cpp instantiation TU + opaque_gen also
rebuilt clean). CURRENT NAMES supersede the history above:

- **`common/offset_file.hpp` → `m2/offset_block.hpp`** and its namespace moved
  `wowlib::formats` → `wowlib::formats::m2` (the engine is M2-only; all
  consumers are nested under m2, so their unqualified lookups still resolve;
  shared reflection helpers are spelled `formats::detail::` from inside m2).
  The CRTP mixin **`OffsetFile` → `M2OffsetBlock`** (user picked "Block" over
  "File" — it also models embedded chunk payloads, not just whole files); its
  marker base `OffsetBase → M2OffsetBase`. `OffsetEntity`, `OffsetReadContext`,
  `OffsetWriteContext` KEPT their names (already clear, not "binary").
- **"binary" jargon purged** across the M2 subsystem (functions, prose, the docs
  generator's regex + config). Key renames: the annotation `wire_after →
  offset_after` (annotations.hpp; re-documented as a POSITIONAL LAYOUT anchor —
  it is about correct positional READING of the flat MD20 layout, NOT about
  byte-perfect writes, which M2 has none of; a field read at the wrong position
  misaligns every offset after it); `wire_size<T,V>() → layout_size<T,V>()`
  (free consteval in m2); free `wire_offset_of<E> → static member
  `M2OffsetBlock::member_offset`; free `entity_image_size(e) → member
  `e.image_size()`; `read_wire/write_wire → read_member/write_member`;
  `M2ArrayWire → M2ArrayRef`. Also `boundaries.hpp m2_wire_version[_range] →
  m2_format_version[_range]` (aligns with the format_version member).
- **Engine encapsulated as M2OffsetBlock member functions** (was a flat
  `detail::` of free functions): the read/write engine is now protected static
  member templates, and the big `read_wire`/`write_wire` (~80 lines each) were
  SPLIT into small per-kind helpers — read side: read_members → read_member →
  {read_array_member → read_array_ref + read_string_block + read_vector_block →
  read_array_elements | read_scalar}; write side mirrors exactly. The V
  template param was DROPPED throughout (it was always Derived::version). Only
  the pure compile-time layout queries (`layout_size`, and the concepts) stay
  free in the m2 namespace — tests treat them as white-box queries and they are
  genuinely entity-agnostic; NOT hidden in a detail namespace.
- **Named concepts replace the obscure `is_vector_v||is_string` dispatch**:
  `OffsetArrayMember` (vector/string → M2Array reference), `InlineRecordMember`
  (a nested record recursed inline), `InlineScalarMember` (trivially-copyable
  raw bytes), `OffsetStringMember`. Every member function carries full Doxygen
  (param/tparam/return/throws) or welder annotations where welded.
- Every consumer include updated to `<wowlib/formats/m2/offset_block.hpp>`; the
  bindings matrix's explicit instantiations became `m2::M2OffsetBlock<...>`
  (legal from the enclosing `wowlib::formats` namespace via the qualified name).
  Test `test_m2_wire_layout.cpp → test_m2_layout.cpp`.
- **M2<V>::read_chunked and ::write decomposed** (m2.hpp, same sweep): the two
  ~90/~200-line fs-I/O methods split into small phase helpers that read as a
  table of contents. read_chunked → load_skeleton / read_chunked_body /
  read_chunked_skins / read_bone_files / read_physics. write → a version
  dispatcher (pre-WotLK inline / write_monolithic / write_chunked, mirroring
  read) over write_body_image (shared body-write + num_skin_profiles stamp) +
  write_bone_files / write_plain_anims / write_skeleton_satellites /
  write_chunked_skins / write_chunked_phys. GOTCHA: a helper must NOT name the
  constrained alias `M2ChunkedFile<V>` in its SIGNATURE — the alias is
  `requires (V >= m2_chunked_container)`, and member DECLARATIONS instantiate
  for every V when M2<V> does, so WotLK failed the alias constraint. Take the
  stream by deduced `auto&` (resolved only at the Legion+ call site) instead;
  same reason the sequence table flows through `const auto&` (also dodges the
  root-vs-skel M2Sequence version mismatch, exactly as AnimBuffers::sink does).
  The `auto&` makes the helper a member template, but it is only ever called
  with `M2ChunkedFile<V>&`, so it is one instantiation per V — same set the end
  user gets, no extra codegen.
- **Polish (same sweep, user follow-up):** `(root.global_flags & 0x2000u)` →
  `has_flag(root.global_flags, GlobalFlags::ChunkedAnimFiles)` (no hardcoded
  flag bits). Full-container index loops in m2.hpp use `std::views::enumerate`
  (and the SFID view/LOD split uses `enumerate | take/drop`); the
  read_chunked/write_chunked TAILS are monadic `.and_then(...).transform(...)`
  chains on `Result` (= `std::expected`). offset_block.hpp engine helpers took
  `const` on their immutable by-value params (the public read/write API params
  stay clean). The recursive per-element loops (read/write_array_elements) stay
  index-based — enumerate's signed index would fight the `ref.offset + i*elem`
  pointer math (sign-conversion). No C-style casts existed to remove.
