# Formats subsystem architecture

Chunked-format serialization (`src/wowlib/formats/`). Milestone 1 (2026-07-19,
commits b986d1d + 22199bc): chunk framework + WMO with Python/Lua bindings.
Refactor sweep (2026-07-19, uncommitted at time of writing): file layout,
naming, all versions + all documented chunks — reflected below. Plan of
record: `~/.claude/plans/mighty-swimming-falcon.md`.

## File layout (post-sweep)

- `formats/common/` — everything format-agnostic, reused by M2/ADT/... later:
  `fourcc.hpp`, `annotations.hpp` (chunk/since/until/optional/header/container/
  repeats — the annotation vocabulary stays snake_case), `chunked_file.hpp`
  (ChunkExtras, ChunkedFile, ChunkBlob, Repeated, UnknownChunk, JournalEntry
  AND the serializer engine — one header), `string_block.hpp` (StringBlock),
  `flags.hpp` (has_flag), `types.hpp` (layout math/color primitives). The
  offset serializer is NOT here — it is M2-only, so it lives in
  `formats/m2/offset_block.hpp` (M2OffsetBlock, namespace wowlib::formats::m2;
  see the 2026-07-27 sweep in m2-architecture.md). `sequence_data`/`gated_by`/
  `offset_after` are the offset-only annotations in `annotations.hpp`.
- **Header-only parts define where they declare** (user rule, 2026-07-25): no
  vocabulary-header/engine-header splits — the former `serializer.hpp` and
  `offset_serializer.hpp` were inlined into `chunked_file.hpp`/`offset_block.hpp`
  (for the offset engine, the whole read/write engine is now protected member
  functions of M2OffsetBlock rather than trailing free functions). A separate
  definition file is only warranted when it is a `.cpp` for a non-templated
  class (core/path, fs/*).
- `formats/wmo/` — **the directory tree mirrors the namespace tree** (user rule,
  2026-07-20), so each sub-namespace is a subdirectory. Reorganized 2026-07-23 so
  each entity owns its chunk binary structs as a nested `chunks/` subdir/namespace
  (user rule): the submodules are `wmo`, `wmo.root`, `wmo.root.chunks`, `wmo.group`
  and `wmo.group.chunks`, and `root`/`group` are Python *packages*:
  - `boundaries.hpp` (`wmo`) — wmo_version_v17 + the two *layout*-pivot constants
    only (wmo_batch_large_material, wmo_split_groups); chunk-presence boundaries
    are no longer named here (see below).
  - `wmo.hpp`/`wmo.cpp` (`wmo`) — the WMO assembly + WMOBase, wmo_versions, the
    per-namespace welded aliases (X-macro) and extern/instantiations.
  - `convert.hpp` (`formats`) — WMO's `supported_versions<wmo::WMO>` specialization
    (moved out of wmo.hpp) + future `convert_step` overloads; include this, not
    wmo.hpp, to convert across versions.
  - `root/root.hpp` (`wmo::root`) — WMORoot + WMORootBase.
  - `root/chunks/` (`wmo::root::chunks`) — root binary structs split by chunk family:
    `header.hpp material.hpp structure.hpp light.hpp doodad.hpp environment.hpp`.
  - `group/group.hpp` (`wmo::group`) — WMOGroup/WMOGroupBody + their bases.
  - `group/chunks/` (`wmo::group::chunks`) — group binary structs +
    SMOGroupHeader/SMOBatch bases: `header.hpp geometry.hpp light.hpp liquid.hpp`.
  (data_structs.hpp — a single 1247-line file — was split into these; "avoid
  extremely long files, split logically" is a user rule.)
- **Chunk-member annotation style** (user rule, 2026-07-23): one annotation per
  line in a vertical `[[ … ]]`, ordered `chunk()` first, then the format
  annotations (since/until, optional/header/container, repeats), then welder's
  (mark::*), with `welder::doc` ALWAYS last — a raw string literal when multiline.
- Type naming: PascalCase classes everywhere (StringBlock, ChunkExtras,
  Repeated); binary structs keep the client's canonical spellings (SMOHeader,
  CAaBspNode); **WMO keeps its acronym** (WMORoot, WMOWotlk — not WmoRoot).
  Flag namespaces became welded scoped enums (GroupFlags, MaterialFlags,
  HeaderFlags, PolyFlags, DoodadFlags, GroupFlags2) whose enumerators carry
  `Name [[=welder::doc("…")]] = value` (welder 04fded7+ documents enumerators —
  folded into the enum class docstring's `Attributes:` section; annotation goes
  AFTER the name); binary fields stay plain ints (combined bit values are not valid
  enumerators) tested via `has_flag(value, GroupFlags::x)`.

## Core design

- **Version axis**: `ClientVersion` as NTTP (`WMO<versions::wotlk>`). Layout
  changes = constrained partial specializations (`requires (V < boundary)`) keyed
  on the two named layout pivots in boundaries.hpp (wmo_batch_large_material
  7.0.1.20740, wmo_split_groups 9.2.0.42423). **Chunk-set changes** =
  `since()`/`until()` annotations that now carry the chunk's *own* exact client
  version INLINE (`=since(ClientVersion{8, 1, 0, 27826})`), sourced per-chunk from
  wowdev.wiki (user rule, 2026-07-23) — the old shared "feature set" constants
  (wmo_fdid_refs, wmo_volumes, …) are gone: they grouped chunks under a guessed
  common build. Notable corrections this brought: MOSI/MODI are 8.1.0.**27826**
  (not 28186), MPVR is 8.3.0.**33775** (not 32044). Cata/WoD-era chunks that
  wowdev leaves build-less stay expansion-level ({4,0,0,0}/{6,0,0,0}).
- **ALL targeted versions are instantiated**: wmo_versions = the 11
  last-minor-of-major releases (vanilla → tww). The single coupling point is
  the `WOWLIB_WMO_FOR_EACH_VERSION` X-macro in wmo.hpp (aliases + externs) /
  wmo.cpp (instantiations); a consteval static_assert checks it against
  wmo_versions, and the bindings static_assert every Expansion enumerator onto
  an instantiation. Only 3.3.5a and 9.2.7 clients are locally testable today;
  the rest await client installs.
- **ALL wowdev-documented chunks are modeled** (root: MOM3/MOUV/MOPE/MOLV/
  MDDI/MPVD/MAVG/MAVD/MBVD/MFED/MGI2/MNLD/MDDL/MOMX added; group: MOGX/MPY2/
  MOVX/MOQG/MOC2/MOTA/MDAL/MOPL/MOPB/MOLS/MOLP/MLSS/MLSP/MLSK/MOP2/MPVR/MAVR/
  MBVR/MFVR/MNLR added, MORB/MOBS upgraded from blobs to structured vectors).
  Undocumented/offset-based layouts stay ChunkBlob (MOTA, MDDL, MPVD,
  MOPB, MOLS, MOMX, MOM3). MLIQ is the exception that was structured: its
  header-driven vertex/tile grid is a `SelfSerializing` leaf (MLIQData,
  group/chunks/liquid.hpp) — the concrete precedent for the planned generic
  offset_view member kind. v14-alpha-only chunks (MOLM/MOLD/MOIN/old-MOLV/
  MPB*) are deliberately unmodeled; MLSO/MOS2 are binary-only; MFOB (12.1)
  postdates the range.
- **read()/write() are entity METHODS**: `ChunkedFile<Derived>` (CRTP mixin
  over ChunkExtras, chunked_file.hpp) declares them; definitions sit at the
  bottom of the same header; the engine is `detail::read_entity`/`detail::write_entity`
  (no more free `formats::read<E>`/`write` wrappers). The ChunkedFile bases
  are extern-templated in wowlib::formats (an explicit instantiation must sit
  in the template's enclosing namespace) so serializer expansion stays in
  wmo.cpp.
- **Byte-perfect round-trip is a tested guarantee**: reads are chunk-order
  independent (fourcc dispatch); every encounter lands in the journal; writes
  replay it; unknown/duplicate/capacity-overflow chunks are preserved verbatim;
  <8 trailing stray bytes kept. Fresh entities write declaration order.
- **Member kinds are type-driven** (chunked_file.hpp dispatch): trivially-
  copyable struct = data chunk (exact size), `std::vector<T>` = array chunk
  (divisibility), **SelfSerializing** (concept: read(span)/write(FileBuffer&)/
  empty()) = StringBlock and ChunkBlob own their payload encoding,
  `Repeated<T,N>` (+`repeats(n)`), nested ChunkedEntity (+`container`),
  `header` members = raw prelude.
- **StringBlock stores DECODED entries** ((offset, value) pairs + the original
  blob size), not the raw blob: write() lays strings back down over a
  zero-filled buffer of the recorded size — lossless for any client blob
  (padding is zero runs by construction, unterminated tails work because only
  value bytes are copied). at() is binary-search, suffix-aware (mid-entry
  offsets return the tail — some data references shared filename tails);
  add() appends past the end so stored offsets never move. Entry is a welded
  nested struct.
- **Presence policy**: `optional` per member; nearly everything optional (only
  MVER/MOHD/MOGP required). Optional plain-data chunks are unobservable and
  must not be declared — single-record optional chunks (MDAL, MOGX) are
  modeled as vectors instead (engagement = non-empty).
- **Conversion** (convert.hpp): supported_versions<wmo::WMO> lists all 11.
  No real steps yet. `has_convert_path<E, From, To>()` (consteval) reports
  whether the full step ladder for a pair exists — runtime-dispatching callers
  (Python factories) use it to degrade a missing ladder to a NotImplemented
  Result instead of convert()'s static_assert.

## Versioned-trait layout (conditional bases) — 2026-07-24

Replaces the earlier "one struct with EVERY field, `since`/`until` gating what
serializes." A version's entity now carries ONLY the chunks that version defines, so
setting an absent field is a **compile error** (Python `AttributeError`) — not a
silently-dropped write. Migrated: `WMOGroupBody`, `WMORoot` (commits 381043e,
bdd2e94). The single-struct model described above is history; this is how versioned
entities work now.

- **Range-traits**: version-gated chunk members move into small **unwelded** structs
  in the entity's `detail::` namespace, one per availability RANGE (root:
  RootLegion/73/Pre81/81/83/90/91/110/111; group: GroupBodyCata/Wod/Legion/81/83/90/
  100). Chunks sharing a `since` (and `until`) go in one trait — well-scoped ranges
  keep the count small (~7–9). Members keep their full `[[chunk/since/until/doc/mark]]`.
- **Conditional inheritance**: `slot<V, Since, Trait, Until = version_never_removed>`
  (`formats/common/version_slot.hpp`) = `std::conditional_t<(V >= Since && V < Until),
  Trait, absent<Trait>>`. The entity inherits one slot per trait; inactive → a
  DISTINCT empty `absent<Trait>` (one per Trait, so several inactive slots aren't a
  duplicated base; EBO elides them). A REMOVED chunk (root MOSB, gone at 8.1) is a
  trait with `Until` set. Always-present + V-dependent members (header, `batches<V>`)
  stay own members.
- **welder needs NO changes**: it flattens a non-welded base's public members onto the
  derived binding (`carriage.hpp` `bind_members` over `public_bases`; `reflect.hpp:396`),
  reading annotations off the member's DECLARING class — so an active trait's
  marks/docs survive and `WMORootWotlk` binds only its fields. (We first spiked C++26
  `std::meta::define_aggregate` member injection: it works and MEMBER annotations
  survive, BUT injected CLASS annotations do NOT — no weld marker, would need explicit
  tack-`weld_type` — and `^^std::uint32_t` on a bare alias fails. Conditional bases are
  simpler and keep the Python layer identical; old type-level tricks beat reflection
  here. Spikes were throwaway.)

**Serializer** (chunked_file.hpp engine half):
- `members_of<E>` now **flattens public bases** (`collect_members`, mirroring welder)
  so it sees trait members. Base bookkeeping (ChunkExtras journal/unknown/trailing)
  comes along but carries no `chunk()`, so every chunk loop skips it. The journal's
  member index just indexes the flattened list (stable ⇒ round-trip unaffected).
- Fresh-entity write order is an **authoritative `static constexpr chunk_order`** (a
  fourcc array) on the entity — the by-trait flatten order is NOT canonical.
  `write_order<E>()` follows it when present (detected via `requires { E::chunk_order; }`;
  it and `version` are *static* members, invisible to welder + `members_of` which use
  *nonstatic* members), else declaration order (un-migrated entities unchanged). A
  compile-time check requires the table to list every chunk member exactly once.
  `version_active` is now largely redundant (an inactive chunk's member doesn't exist);
  `since`/`until` survive on trait members as doc metadata (the docs badges read them).

**Latent bug the migration caught**: `WMO::read` (wmo.cpp) read `root.group_fdids`
(GFID, Legion+) unconditionally — a compile error once pre-Legion roots lack it.
Guarded with `if constexpr (requires { root.group_fdids; })`; pre-Legion locates groups
by path. **Rule**: library/test code touching a version-gated member must `if constexpr
(requires { … })`-guard the access.

**Docs interaction** (`docs/wmo_reference_impl.py`): no single version has the superset
(removed chunks). Each side's C++ slice reads from the entity header's `namespace detail`
(traits) through the struct, since the range members now live in the traits. The fields
page (`content/python/wmo/fields.md`) no longer lists a class; it carries two markers
(`<!-- wmo-{root,group}-fields -->`) that `on_page_markdown` replaces with **per-category
`### Section` headings + generated mkdocstrings `:::` directives** (`_fields_markdown`):
  - Chunks are grouped by a finer-grained taxonomy — `CATEGORY_ORDER` / `FOURCC_CATEGORY`
    (Header · Materials · Geometry · Collision · Doodads · Lights · Portals · Fog ·
    Volumes · Liquid · Group table · Culling · Skybox). This map is presentation-only
    (not a C++ annotation); a chunk FourCC missing from it is logged at build time and
    dropped into Header, so drift is loud, never silent.
  - Each directive uses `show_root_heading: false` + `members: [...]` to render just that
    category's members (ordered by the entity's `chunk_order`). Live fields come from the
    representative (latest) class; a **removed** chunk renders from the latest version
    class that still declares it (`_field_source_class`, e.g. root MOTX/MODN/MOSB from
    `WMORootLegion`) — so added and removed chunks sit together in-section, each with its
    since/until badge. There is no separate "removed" section.
  - Generating markdown (not surgical HTML) keeps the right-hand TOC correct: the toc
    extension sees the real `###` headings.
  - Flag/enum types on the chunk pages get a wowdev badge + a same-page backlink to the
    binary struct that carries the bits, via `ENUM_CHUNK` (enum → (FourCC, struct), e.g.
    `PolyFlags → (MOPY, SMOPoly)`; templated structs alias, `WMOGroupHeader → SMOGroupHeader`).
  - Binary integer widths: nanobind flattens every `std::uintN_t`/`std::intN_t` to Python
    `int`, so the on-disk size is parsed back from source and the rendered signatures
    re-annotated `int → Annotated[int, uint32]` (arrays → `list[Annotated[int, uint8]]`,
    signed → `int16`). Chunk pages read binary-struct fields (`_struct_int_fields` →
    `_annotate_int_widths`); the fields page reads entity members' element type
    (`_annotate_entity_int_widths`, handling both scalar `mver` and the `list[int]`
    that `_coerce_vectors` produces for opaque int vectors). Both run in `on_post_page`
    — the `int` type is an unresolved autoref until then.

## Range-collapsed instantiation (2026-07-25, user item: "we overinstantiate")

- Every versioned family now instantiates ONE class per REAL content
  permutation, not per release. Mechanism
  (formats/common/version_range.hpp): the annotated struct moves into a
  nested `detail` namespace and the public name becomes a CANONICALIZING
  alias — `template<V> using Foo = detail::Foo<canonical_version(V, pivots,
  grid)>` — flooring V over the family's declared pivots to its range's
  first grid version. Pivot lists + grids live in each format's
  boundaries.hpp (generous listing is free: a pivot that doesn't separate
  two grid versions is a no-op). Constrained families keep a requires on the
  ALIAS so facade era-subsetting (family_has) still works.
- Welded names carry RANGE suffixes: single version "Wotlk", interior
  "CataToMop", trailing "LegionPlus" (stable when new releases extend the
  range). The welding X-macros are per-family RANGE tables, consteval-checked
  by ranges_valid() against the pivots (suffix strings included, via #Suffix
  stringization) — rows, names and pivots cannot drift. The facade
  (def_for_version/def_any_alias/convert sigs) takes (pivots, grid) per
  family and derives the same names via range_suffix(); several Expansion
  Literals map to one concrete class and AnyX unions fold per range (AnySkin
  = SkinWotlk | SkinCataPlus; AnySkeleton is a single class, not a union).
- Collapse achieved: M2 tracks/records 11→2-4 each, Skin 9→2, Skeleton+Skel
  payloads 5→1, M2Data 11→6, WMORoot 11→5, WMOGroup(Body) 11→7,
  SMOBatch/SMOGroupHeader 11→2, assemblies 11→10 (M2) / 11→8 (WMO).
- convert() walks the CANONICAL ladder: a new `version_pivots<E>` trait
  (specialized beside supported_versions for BOTH the alias and detail
  spellings — an alias template is not identity-equal to its target for
  template-template matching, and deduction sees the detail one) + a
  next_canonical() step; ranges cost no convert_step.
- GOTCHAS learned:
  - Inside a `detail` block, a sibling RAW template SHADOWS the
    canonicalizing alias — every cross-family member reference must go
    through the alias (records::M2Track<...>, skin::M2SkinProfile<...>,
    m2::Skeleton<...>), or never-welded raw instantiations leak into member
    types (import-time std::bad_cast from eager NSDMI conversion; found via
    lldb break on __cxa_throw).
  - Explicit instantiation must name the detail:: template (an alias cannot
    head one), and same-type rows across a range must be driven by the
    family's OWN range table or you get duplicate-instantiation errors.
  - gcc 16 cannot mangle the span-converting canonical_version call in a
    dependent function signature ("sorry, unimplemented: mangling
    view_convert_expr") — facade glue goes through a concrete_of<F, X>
    nested-typedef indirection.
  - Explicit class instantiation instantiates CONSTRAINED member
    declarations' signatures too: an era-gated method must not spell an
    era-constrained alias in its parameters (M2::read_skin_into takes
    `auto& out`).

## Instantiation & I/O layout (2026-07-24 refactor)

- **The library ships NO explicit instantiations.** The all-versions
  entity + serializer matrix moved to bindings/instantiations/
  ({wmo,m2}.hpp extern declarations included by every binding TU; one .cpp
  per format expands the matrix, parallel to the welder walk). Library
  consumers implicitly instantiate exactly the versions they use.
- **fs-level read/write definitions live INLINE at the bottom of the entity
  headers** (m2.hpp, skeleton.hpp, wmo.hpp; the separate io.hpp headers were
  dissolved 2026-07-25 — user: order-dependent includes with no use case for
  a template head without its definitions). The library ships NO explicit
  instantiations; every consumer TU implicitly instantiates exactly the
  versions it uses. X-macros + consteval version-coverage checks stay
  in the library headers.
- **Derived binary counts** (2026-07-24, survey-grounded: 1983 3.3.5a + 8146
  9.2.7 roots): write_entity invokes an optional entity hook
  `patch_chunk(fourcc, span<std::byte>) const` on each finished chunk
  payload — WMORoot stamps MOHD n_groups (from MOGI), n_portals,
  n_doodad_sets; those three are mark::exclude'd (hidden from bindings).
  n_textures / n_lights / n_doodad_defs / n_doodad_names are NOT derivable
  (real files store legacy values: 387/1983 n_textures, 421 n_doodad_defs,
  3 n_lights in 9.2.7, all fdid-era n_doodad_names) — they stay stored,
  quirk documented per member; deriving them would break byte-perfect
  round-trips. WMO::read counts groups from MOGI (group_infos.size());
  WMO::write validates MOGI size == groups.size(). The offset engine's
  counterpart is the consteval static member `M2OffsetBlock::member_offset(name)`
  (offset_block.hpp; was the free `wire_offset_of<E>`) for stamping into a
  written image (M2 num_skin_profiles); its `template for` ranges must be
  `static constexpr` or constant evaluation fails.

## Reflection findings (gcc 16.1, `-freflection`)

Canary: `tests/unit/test_reflection_spike.cpp` — keep compiling. Everything
from milestone 1 still holds (annotations with ClientVersion payloads read
through instantiations; `template for` + splicing; constrained partial
specializations stay trivially copyable). New gotchas from the sweep:
- Enumerator annotations go AFTER the name (`name [[=doc("…")]] = 0x1`), never
  before (`[[=doc]] name = 0x1` doesn't parse). The trailing position is no longer
  pointless: welder 04fded7+ documents enumerators (folds them into the enum class
  docstring) — the flag enums now use `welder::doc` there, not `/**< */`.
- A `using W = WMO<to_client_version(X)>` inside a lambda nested in a
  `template for` body fails ("incomplete type W") — hoist the body into a
  function template (`def_wmo_factories<X>`) and call it from the loop.
- A splice as a template argument must be parenthesized:
  `def_wmo_factories<([:e:])>(...)`.

## Bindings (no welder changes)

- **Naming**: `wowlib_python_naming` (bindings/python/wowlib_module.cpp)
  replaces welder's pep8 style — snake_case base with class/enum/enumerator
  transforms VERBATIM, so SMOHeader/WMORoot/FileDataID keep their acronyms
  (pep8 would produce SmoHeader/WmoRoot/FileDataId). Lua stays
  welder::naming::snake_case (wmo_wotlk, smo_header — unchanged shape).
- Per-version classes weld through namespace-scope aliases (WMOWotlk, ...) each
  declared **in its family's namespace** (X-macro, wmo.hpp), so they surface
  under the matching submodule (`wowlib.formats.wmo.root.WMORootWotlk`, etc.).
- **The versioned-format facade is 100% NATIVE — no stub post-processor, no ABC**
  (rewritten 2026-07-20; the user rejected the facade_stub.py post-processor as
  unmaintainable — "does nb::sig cover it?" — and yes it does):
  - **Welded empty C++ base struct per family** (`WMOBase` weld_as "WMO",
    `WMORootBase`, `WMOGroupBase`, `WMOGroupBodyBase`, and — on the trivially-
    copyable binary structs — `WMOGroupHeaderBase`/`WMOBatchBase`). The per-version
    template *inherits* it (`WMO<V> : WMOBase`; `WMORoot<V> : ChunkedFile<...>,
    WMORootBase` — ChunkedFile is a non-welded mixin so nanobind's single-welded-
    base rule holds). welder registers the base natively → `class WMOWotlk(WMO)`
    in the stub AND real `isinstance`/`issubclass`, ZERO glue. Binary-struct bases
    are empty → EBO keeps the memcpy layout (0x44/0x18 static_asserts prove it;
    `WOWLIB_EMPTY_BASES` = `__declspec(empty_bases)` guards MSVC — gcc-only today).
  - **for_version / read / write / convert are nb::sig merged overloads on the
    base**: `nb::cpp_function(fn, nb::name("for_version"), nb::scope(base),
    nb::sig(...))` — the same nb_func merge `nb::class_::def_static` uses — so per-
    expansion Literal overloads (+ Expansion→AnyX fallback) render as typed
    @overload blocks and mypy narrows `for_version(Wotlk)->WMOWotlk`,
    `convert(Cata)->WMOCata`. read/write are (FileSystem,FileKey) + (buffers)
    overloads (buffers accept bytes / bytes-like / BinaryIO via to_buffer + the
    span caster). GOTCHA: the merge is by `name`+`scope`, NOT attr assignment.
  - **read/write on the assembly live on WMOBase, not the concrete.** `WMO<V>`'s
    own read/write(fs,key) are `mark::only(welder::lang::lua)` (Lua's WMO
    load/save); Python excludes them so the concretes inherit the richer base
    overloads unshadowed. GOTCHA: `weld(lang::lua)` does NOT restrict — `mark::only`
    is the "bind for these langs only" knob (annotations resolution rule).
  - **AnyX unions are REAL runtime objects, not stub-only** (2026-07-22): the
    facade binds `AnyWMO = WMOVanilla | ... ` as an actual `types.UnionType` on
    each submodule at import (`def_any_alias`, facade.hpp — folds the concrete
    classes with `operator|`, derived from the same `expansion_enumerators` walk),
    so `from wowlib.formats.wmo import AnyWMO` works and the alias is usable in
    annotations AND `isinstance` (a UnionType supports instance checks on 3.10+).
    The runtime side auto-grows with a new Expansion. The STUB spelling still comes
    from `stub_patterns.nb` (PATTERN_FILE), because nanobind 2.13's stubgen
    mis-renders a `types.UnionType` as the invalid `types.UnionType[...]` subscript
    (`type_str`: origin+args) — so each per-member pattern both suppresses that
    broken auto-emit and writes the correct `AnyX = C0 | ...`. The pattern list is
    the one hand-maintained coupling point with wmo_versions + the X-macro; check.py
    guards both halves (a runtime isinstance test + a mypy narrowing test).
- **Verb unification — read()/write() everywhere** (C++ + bindings). WMO's C++
  surface: `read(fs,key)` (load) + excluded `read(root_span, group_spans)`
  (parse) + `write(fs,key)` (save); write_root/write_group gone (the library
  speaks whole entities, never single groups). Sub-entities keep ChunkedFile
  `read(span)`/`write()->bytes` on the concrete (their bases are for_version-only).
- check.py/check.lua: rewritten for the split submodules + native facade (for_version,
  `WMOWotlk.__bases__ == (WMO,)`, real isinstance, read-is-inherited-not-reshadowed,
  bytes/bytearray/BytesIO round-trip, write-to-sink, convert, mypy narrowing).
  Real-client Python round-trip byte-perfect (probed, 12 groups).

## Known issues / deferred

- ~~FileSystemSettings keyword-defaults ctest failure~~ RESOLVED 2026-07-20:
  it was never a welder regression — the pin had been dropped to the commit
  BEFORE the NSDMI-defaults feature (see deps-build-notes, "Bumping the welder
  pin"). Repinning to 57e7747 surfaced the real formats-side bug: welder
  registers NSDMI default values EAGERLY, so a namespace's welded types must
  register before any aggregate that uses them as defaults — and wowlib.hpp
  declared the doc'd `wmo` namespace inside the block that first opens
  `formats`, making wmo formats' FIRST member (members_of = declaration order,
  and a doc pre-declaration fixes the position). SMOHeader then welded before
  CArgb/CAaBox existed → `ImportError: std::bad_cast` at module init. Fix: the
  doc'd `wmo` pre-declaration in wowlib.hpp moved below the formats/common
  includes. Rule for new formats: pre-declare the format's doc'd namespace
  AFTER every namespace whose types appear as NSDMI defaults in its structs.
- **Opaque vectors SHIPPED** (2026-07-22, welder 31b0801): every WMO `std::vector`
  binds by reference (live in-place mutation + zero-copy NumPy) via welder's
  opaque-container generator — see bindings-notes.md. Includes the NTTP-versioned
  `WMO::groups`/`WMOGroupBody::batches`. Wrappers are named by a
  `transform_opaque_container` style hook (`VectorSMOMaterial`, `VectorC3Vector`,
  `VectorWMOGroupWotlk`). Every bound WMO vector also carries
  `[[=welder::mark::no_reassign]]` → the property is read-only (mutate in place, but no
  whole-attribute reassignment). The excluded `Repeated<>` texcoords/vertex_colors and
  the `ChunkBlob` members are unmarked.
- Deferred: ADT; the remaining offset-based intra-chunk data (MOTA/MDDL/
  M2Array — planned as an `offset_view` member kind generalizing the MLIQData
  approach); real convert steps; Repeated<>
  bindings (sequence protocol; texcoords/vertex_colors still mark::exclude'd);
  integration coverage for the 9 versions without local clients.

## WDT + WDL (2026-07-26)

Both formats shipped end-to-end (C++ + tests + bindings + docs), grounded in a
FULL-corpus survey of both clients (106 + 805 maps; scratchpad script walked
raw chunk streams first, entity round-trip after). Survey CORRECTIONS to
wowdev.wiki, baked into the models:
- WDL MWMO/MWID/MODF are NOT removed in Legion: 9.2.7 WMO-only maps still ship
  them (206 files) — modeled with no `until()`; the MLDD/MLDX/MLMD/MLMX set is
  since(Legion) alongside.
- WDL MSSF is documented DF+ but present in 9.2.7 → since(SL). Undocumented
  MLDF/MLDL/MLDB/MLMB blobs observed in 9.2.7 WDLs → optional members since(SL)
  (MLDL is a u32 vector per the ADT chunk of the same name; rest ChunkBlob).
- _lgt 9.2.7 files carry MPL3/MSLT/MTEX/MLTA only (no MPLT/MPL2 back-compat);
  chunk order MVER MPL3 MSLT MTEX MLTA.
- _mpv PVMI/PVPD/PVBD REPEAT as ordered groups (up to ~13 per file); PVMI's
  record size keys on the FILE's own MVER payload, not the client → per-group
  ChunkBlob.
- MAOF nonzero slots == MARE chunks, offsets point at the MARE chunk HEADERS
  in row-major slot order, MAHO count == MARE count — 100% of 911 files.

**Framework additions** (chunked_file.hpp / annotations.hpp):
- `=formats::repeating` on a `std::vector<Element>` member: one chunk per
  ELEMENT, unbounded (vs `Repeated<T,N>` slots; vs plain vector = one array
  chunk). Element reads by the usual kind dispatch (struct exact-size, vector,
  SelfSerializing). Opaque vector binding covers it for free.
- `Result<std::optional<std::vector<JournalEntry>>> resequenced_journal() const`
  hook: replaces the replay order for one write (nullopt = stored journal).
  `detail::fresh_journal(entity)` builds the canonical-order starting journal;
  `detail::chunk_member_index<E>(magic)` maps fourcc -> flattened member index.
- `Result<void> patch_file(std::span<std::byte>) const` hook: sees the WHOLE
  serialized image after the trailing bytes — for binary fields that reference
  bytes written after their own chunk (WDL MAOF; ADT MHDR/MCIN later).
  patch_chunk stays for single-chunk stamping.

**WDL model** (formats/wdl/, one entity, 5 ranges VanillaToTbc/WotlkToWod/
LegionToBfa/ShadowlandsToDragonflight/TheWarWithin): per-tile MARE/MAHO/MAOE/
MAOC are `repeating` members pairing BY ORDINAL — i-th heightmap ↔ i-th
nonzero tile_offsets slot (row-major), i-th hole ↔ i-th heightmap. MAOF offset
VALUES are DERIVED (patch_file restamps every write; verified byte-identical
on unmodified files), only the nonzero pattern is authored. Tile add/remove
triggers resequenced_journal: rebuild = non-tile chunks in chunk_order, then
per tile MARE/(MAOC/MAOE)/MAHO interleaved after MAOF, unknown chunks last;
validations (4096 slots, count pairing, holes all-or-nothing) error as
InvalidEntityState. Sparse MAOE pairing derives from the journal interleave —
welded `ocean_mask_tiles()` method exposes it. Fresh writes REQUIRE the user
to fill tile_offsets (no NSDMI vector default — the eager-default risk).

**WDT model** (formats/wdt/): WDTRoot (MVER/MPHD/MAIN/MAID/MWMO/MODF/MANM-
blob; SMMapHeader has a constrained-specialization layout pivot at 8.1.0.28294
where the unused tail becomes satellite FDIDs) + four satellite entities
(occlusion MAOI/MAOH; lights; fogs; mpv) + a WMO-style assembly whose
satellites are members of version-gated slot traits (occ/lgt since WoD, fogs
since 7.2.5.24076, mpv since 8.0.1.26287). Satellites locate by
"{map}_<suffix>.wdt" convention pre-8.1, MPHD FDIDs after; missing file =
default-empty member, write skips unengaged satellites
(formats::detail::entity_engaged). MAI2 (12.0.5+) postdates the range,
round-trips as unknown. Shared placement records SMMapObjDef/SMDoodadDef live
in formats/common/map_placements.hpp (welded under formats.common; ADT will
reuse). GOTCHA: binary-struct names are FLAT in the opaque-vector namespace —
wdt's light records are MapPointLight/MapPointLight3/MapSpotLight because WMO
already owns PointLight (the opaque generator hard-errors on the collision).
GOTCHA: with a chunks::detail namespace present, the entity alias must spell
root::detail::WDTRoot (bare detail:: ambiguous under the using-directive).

**Bindings**: instantiations/{wdt,wdl}_{ranges.hpp,matrix.inl,.hpp,.cpp} +
formats/{wdt,wdl}.cpp facades, registered in wowlib_module.cpp and — REQUIRED —
in opaque_gen.cpp (the generator only sees containers reachable from welded
per-range aliases; forgetting the include silently leaves new vectors
by-value). WDT verbs live on WDTBase (fs-only, like M2). WDL is a ChunkedFile
whose concretes weld read(bytes)/write() — base-scoped fs verbs would be
SHADOWED in Python attribute lookup, so the (FileSystem, FileKey) overloads
are MERGED into each concrete's chain (nb::cpp_function name+scope merge, one
range representative each). Skeleton had the same wart (fs verbs on
SkeletonBase, shadowed by the concrete's welded pair, unreachable from
Python) — FIXED 2026-07-26 the same way (def_skeleton_fs_verbs in
bindings/python/formats/m2.cpp). Single-range families' AnyX alias
is the class itself, not a UnionType (AnyWDTOcclusion, AnyWDTParticulates,
like AnySkeleton). std::array members bind BY REFERENCE since welder 83abe9d
(fixed-size Array* wrappers: element write-through, zero-copy NumPy for
scalar/POD elements, whole-assignment length-checked; no size-changing ops) —
wowlib's opaque naming hook styles them Array{VerbatimElem}x{N}
(ArrayC3Vectorx3), scalars keep welder's derived names (ArrayShortIntx289);
the docs engine coerces Array* to list[Elem] display exactly like Vector*.

**Docs**: wdt/wdl_reference_config.py registered in FORMAT_MODULES + the
format_reference.py reload shim; wdt has FIVE sides (root page + four
satellite sides sharing satellites.md) and five StructPages sharing chunks.md
(per-marker); vendored wdt/wdl_wowdev_anchors.json; map_placements.hpp joined
COMMON_TYPES headers; guide/maps.md.

## Validation (2026-08-09, stages 1-2: framework + WMO + M2 + ADT)

`validate()` asserts the logical-integrity contracts a file must satisfy to
LOAD in the client — a SEPARATE pass the user invokes before writing; write()
never runs it (user decision). Approved staging: 1 framework+WMO (landed),
2 ADT+M2, 3 WDT/WDL predicate migration + DB + BLP, 4 bindings facade + docs.

- **Vocabulary** (formats/common/validation.hpp): ValidationSeverity /
  ValidationIssue {severity, path, message} / ValidationReport (linter-style:
  collects ALL findings; `ok()` = no errors, `to_result()` folds into ONE
  InvalidEntityState, `prefix_from(mark, prefix)` scopes sub-entity walks).
  SEVERITY POLICY: error = the client would misread a file WE write; warning =
  real client files ship it. A freshly read client file must report ZERO
  errors — the WMO corpus round-trips assert exactly that (the calibration
  loop: write the check as error, run the corpus, demote/delete what fires).
- **Annotations** (annotations.hpp): `count_matches(name, scale=1)` (engaged ⇒
  size()*scale == sibling's; applies per filled Repeated slot),
  `count_multiple_of(n)`, `indexes(name)` (every element < sibling count),
  `indexes_in_root(name)` (checked by the ASSEMBLY against its root entity —
  the member's own entity skips it), `expected_value(v)`, `nonempty`. Sibling
  names resolve via `detail::member_named<E>` at compile time (typo =
  static_assert).
- **Walker** (chunked_file.hpp): `detail::validate_entity(entity, report)` —
  `template for` over members_of (traits flattened), applies the specs, recurses
  into container members with path prefixing, then calls the optional
  `validate_extra(ValidationReport&) const` hook (imperative complement:
  record-interior ranges, flag↔presence, anything cross-member). ChunkedFile
  gains `validate()` + `ensure_valid()` (both `mark::exclude` until stage 4 —
  ValidationReport is not welded yet, binding them would break assert_bindable).
  Index findings are capped at 8 per member + "and N more".
- **WMO**: group MOPY/MOVI/MONR/MOTV/MOCV/MOC2/MOVX/MORB annotated; MOLR/MODR/
  MAVR/MBVR/MFVR/MNLR carry indexes_in_root; group hook = MOBA index/vertex
  ranges + header batch partition (trans+int+ext == MOBA count) + BSP node/face
  ranges + flag↔presence + MLIQ grid arithmetic + MOPL cap 32 + MLSP/MLSK set
  ranges. Root hook = MODS/MOPT/MOVB ranges, MOPR indices, MOGI name offsets,
  MODD name resolution (MODN offsets while engaged, else MODI indices),
  MOMT texture_1 offset, MOLV light indices. `WMO<V>::validate()` (assembly,
  not a ChunkedFile — own method) = root+groups walks + MOGI count == groups +
  GFID coverage + indexes_in_root resolution + header portal slices into MOPR
  + poly/batch material ids < MOMT count (0xFF = collision-only sentinel).
- **Corpus calibration findings** (baked in as comments): vanilla/TBC files
  ship raw FLOAT garbage in SMOMaterial texture_2/texture_3 (Stormwind.wmo,
  Subway.wmo) — only texture_1 is validated; a BfA kultiras group ships
  has_two_mocv with ONE MOCV layer — multi-layer flag shortfalls are warnings
  (base has_vertex_colors with zero layers stays an error); MOGI flags diverge
  from group-header flags on runtime-managed bits in ~every real group — the
  mirror check was DELETED as pure noise. MOHD derived counts (n_groups etc.)
  are not validated (patch_chunk restamps); the stale legacy counts
  (n_textures, ...) are deliberately unchecked.
- Tests: tests/unit/test_validation.cpp (annotation kinds, hooks, assembly
  cross-checks, ensure_valid folding); test_wmo_roundtrip.cpp asserts
  `ensure_valid()` on every corpus WMO after the byte-perfect check.

### Stage 2 (2026-08-09): the walker generalized, M2 + ADT covered

The walker was NOT chunk-specific, so it moved out of the chunk engine:
- **`formats/common/entity_reflect.hpp`** (new) holds the reflection helpers
  ALL three engines share — `annotation`, `version_active`, `is_vector_v`,
  `collect_members`, `members_of`, `member_named`, plus `nested_in_std` /
  `is_std_type`. Same `wowlib::formats::detail` namespace as before, so every
  call site is unchanged; only the header moved (out of chunked_file.hpp).
- **The walker itself now lives in validation.hpp** and drives ANY entity with
  `static constexpr ClientVersion version` (`VersionedEntity`) — chunked files,
  M2 offset blocks, ADT tiles and map chunks alike. `validate_members<V, T>`
  applies the specs; `validate_value<V, T>` dispatches entity / record /
  sequence-of-either and calls `validate_extra`; `validate_entity` is the entry.
  `M2OffsetBlock` gained validate()/ensure_valid() next to ChunkedFile's.
- **Recursion is content-driven**: `has_validation_content<T>()` (consteval)
  answers "can validating this produce anything" — declares validate_extra, or
  carries an annotated member, or holds something that does. Trivially-copyable
  types stop it (binary structs carry no annotations by policy) and so do std
  class types (`is_std_type`), EXCEPT that sequences are followed into their
  element type first. So `vector<M2Bone>` is walked, `std::string` is not, and
  entities pay nothing for members with no contracts.
- **Path prefixing**: `prefix_from` joins a path that starts with `[` without a
  dot, so element walks read `bones[3].rotation`, not `bones.[3].rotation`.
- **Report cap**: `ValidationReport::max_findings` = 1000; `add()` drops past it
  and sets `truncated()`, and every element loop checks `full()` — a corrupt
  million-element array cannot make validating the memory problem.
- New annotations: `count_exactly(n)` (fixed grids), `indexes_optional(name)`
  (the client's "none" sentinel — negative signed, all-ones unsigned — is legal).
- **GOTCHAS (gcc 16)**: a TYPE splice as a template argument needs
  `typename [:...:]` — parenthesizing (`([:...:])`, right for VALUE splices, as
  in def_wmo_factories) makes it an expression splice and fails. And the
  "is this member a nested array or a slot container?" question must NOT be
  guessed from shape: `std::array<u8,4>` (a per-vertex bone quad) and
  `vector<vector<u8>>` (ADT alpha maps) both look sequence-shaped but mean
  different things. Resolution: only `std::vector` elements count as nesting,
  and `Repeated<>` opts into per-slot semantics with
  `static constexpr bool validation_slots = true` (`SlotSequence`). Both
  mis-classifications were caught by the corpus, not by review.

**M2** (`indexes_optional` on sequence/replacable-texture/transparency/
transform/attachment/camera lookups; `count_multiple_of(3)` + `indexes` on
collision_triangles; `count_matches("collision_triangles", 3)` on
collision_normals; `count_matches("timestamps")` on M2Track values — which
covers BOTH eras, the WotLK+ per-sequence arrays pairing element-wise too —
plus FBlock/M2PartTrack). Hooks: M2Root validates the bone hierarchy (parents
exist and PRECEDE their children) and alias-sequence chains (dangling, self-
referential, cyclic); M2SkinProfile validates submesh vertex/index slices
(index_start is extended by `level << 16`) and batch submesh indices. The
assembly `M2<V>::validate()` does the cross-file half: each skin's local->global
vertex lookup into the body's vertices, submesh bone-lookup slices, batch
material/color/texture-lookup resolution, and BOTH bone lookups against
whichever list supplies the bones.
- **Corpus corrections** (baked in as comments): a skel-based model (Legion+)
  leaves `root.bones` EMPTY and keeps its bones in the .skel, so
  bone_lookup_table/key_bone_lookup could NOT stay annotated on the body —
  they moved to the assembly, which picks the effective list. And
  `texture_count` governs only the TEXTURE lookup slice: real files overrun the
  transparency/transform tables freely (3.3.5a humanmale spans 6 over a 2-entry
  table), so only those slices' START entry is checked, and only when the table
  is non-empty.

**ADT** (`count_exactly(mcvt_count)` on heights/normals/vertex_colors/
vertex_lighting, `count_exactly(alpha_texels)` on shadow_map,
`count_matches("layers")` on alpha_maps, `indexes_in_root` on MCRD/MCRW).
MapChunk's hook checks each engaged alpha surface is the full decoded 4096
texels (layer 0's is ignored → warning). `ADT<V>::validate()` does the
tile-wide half: the 256-chunk grid, layer texture ids against MTEX **or** the
MDID FileDataIDs (whichever `uses_texture_fdids` selects), chunk refs into
MDDF/MODF, and placement `name_id`s into the name-offset tables — skipping
records whose `entry_is_fdid` flag makes the id a FileDataID instead.
`chunks_per_tile` (256) is now a named constant in adt/boundaries.hpp.

Corpus assertions: test_m2_roundtrip.cpp and test_adt_roundtrip.cpp (both
helpers) call `ensure_valid()` on every freshly read file. Full local suite
after stage 2: 609k+ assertions, zero findings across all four clients.

## Adding a new format (the recipe)

1. `formats/<fmt>/data_structs.hpp`: binary structs (trivially copyable,
   std::array not C-arrays, size static_asserts, welder::weld + doc, flag
   enums with Doxygen enumerator comments) + version boundary constants.
2. `formats/<fmt>/<entity>.hpp` files: entities `struct E : ChunkedFile<E>`
   with `static constexpr ClientVersion version = V;`, members annotated
   `chunk/since/until/optional/header/container/repeats` in canonical client
   order; an assembly struct with load/save via fs::FileSystem.
3. `formats/<fmt>/<fmt>.hpp`: `<fmt>_versions` + supported_versions
   specialization + a FOR_EACH_VERSION X-macro for aliases/externs (incl.
   `extern template struct ChunkedFile<...>` in wowlib::formats);
   `<fmt>.cpp`: out-of-line defs + the instantiation X-macro; add to root
   CMakeLists.
4. Tests: layout static_asserts; synthetic parse; integration round-trip
   (memcmp == 0) with the divergence diagnostic + unknown-chunk histogram
   pattern from test_wmo_roundtrip.cpp.
5. Bindings: add a `def_family<F>(wmo, "Name", doc)` call per version-differing
   family and (for entities with verbs) a `def_wmo_verbs`-style glue; a new
   Expansion enumerator grows every facade automatically (template for). Extend
   facade_stub.py's FAMILIES if a new family; stub OUTPUT entry if a new submodule.
6. wowlib.hpp: pre-declare the format's doc'd namespace BELOW the
   formats/common includes (namespace position = submodule weld order, and
   NSDMI defaults of common types convert eagerly at registration — see Known
   issues, 2026-07-20).

### Stage 3 (2026-08-09): WDT/WDL migrated, ClientDB + BLP covered

- **WDL: the write-time guard and validate() are now ONE hook.** The tile-table
  pairing invariants (MAOF slot count, the ordinal pairing that makes the i-th
  nonzero slot own the i-th heightmap, all-or-nothing hole masks) moved out of
  `resequenced_journal()` into `validate_extra`; the rebuild path calls the hook
  and folds the report through `to_result()`, so write() still fails with
  InvalidEntityState and the existing pairing tests pass unchanged. Named
  constants replaced the `64 * 64` literals (`wdl_tile_slots`, `wdt_tile_slots`).
- **WDT** gained `expected_value` on MVER plus a hook for the positional tables
  (MAIN and, when engaged, MAID must both cover the full 64x64 grid — the client
  indexes them as `[y * 64 + x]`) and the at-most-one global WMO placement.
- **BLP is unversioned**, so it does NOT go through the reflective walker (its
  `version` member is a u32 format field, not a ClientVersion — the
  VersionedEntity concept correctly rejects it). `BLP::validate()` is a plain
  method in blp.cpp: base level present, non-zero dimensions, mip count within
  the header's 16 slots, and each level's payload covering the pixels its
  dimensions imply.
  - **Corpus corrections**: a SHORT payload is a documented client quirk for the
    block/raw encodings (the decoders pad with transparent black) → warning; for
    palettized data the client would index past the buffer → error. And
    `alpha_depth` carries junk in shipped files (3.3.5a Textures/SunGlare.blp
    ships 136 on a DXT texture), so a bad depth is an error only for Palettized,
    where it actually sizes the alpha plane.
- **ClientDB** (`Table<Record>::validate()`): id uniqueness — gated on the
  schema actually HAVING an `$id$` column, since plenty of client tables are
  keyless lookup rows (CharBaseInfo, ItemSubClass, ...) — and no embedded NUL in
  any string or LocString slot, which the string block would silently truncate.
  - **A "value fits its column" check was written, then DELETED as dead code**:
    a column's width IS its member's width (schema.hpp derives one from the
    other) and the WDC writer sizes each bit-packed field from the actual value
    range it is given (`write.cpp`: `min(unsigned_width(hi), col.bits)`), so no
    value reachable through the typed API can overflow what encodes it. The
    premise "the encoder writes the low bits of whatever it is given" was wrong.
    A unit test that could not be made to fail is what surfaced it — worth
    remembering as the smell.
- Corpus assertions now cover every format: the three `dbc_corpus.hpp` sweeps
  call `ensure_valid()` per table, and test_blp_roundtrip / test_wdt_wdl_roundtrip
  do the same per file.

## MDAL is MoP+, not WoD+ (2026-08-10)

Corrected from the nightly audit's `unknown_chunks` tally, which is exactly what
that tally is for: 5.4.8 carried **421 MDAL chunks that wowlib was not modelling**
(they round-tripped as unknown, so nothing was lost — but the data was
unreachable). Sampling the local clients put the debut at MoP: 400 WotLK groups
and 700 Cata groups carry none, and the audit's example asset
(`goblin_kezan_mine_blocked.wmo`) does not exist in 4.3.4 at all. wowdev dates
MDAL to WoD but flags it "could have been added earlier" — unverified, and wrong.

The fix is NOT just the `since()`: `builds::MoP` had to become a
`wmo_group_pivots` / `wmo_assembly_pivots` entry, because without it
`WMOGroupBody<mop>` canonicalizes onto the Cata instantiation and the slot would
be evaluated at Cata — the chunk would stay invisible for the very era that
introduced it. That splits the `CataToMop` range into `Cata` + `Mop` for the
group and assembly families (welded names, `wmo_ranges.hpp`, `stub_patterns.nb`,
and the `convert(Cata)` reveal in tests/python/typing/test_facade.mypy-testing).
MDAL moved from `GroupBodyWod` into a new `GroupBodyMop` trait.

Guarded by "MDAL is a MoP chunk, not a WoD one" (test_wmo_binary_layout.cpp):
the member exists on MoP/WoD and not on Cata/WotLK, MoP is provably a distinct
instantiation, and a synthetic MDAL parses into `ambient_color_override` rather
than `unknown` while still rewriting byte-for-byte.
