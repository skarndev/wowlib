# Formats subsystem architecture

Chunked-format serialization (`src/wowlib/formats/`). Milestone 1 (2026-07-19,
commits b986d1d + 22199bc): chunk framework + WMO with Python/Lua bindings.
Refactor sweep (2026-07-19, uncommitted at time of writing): file layout,
naming, all versions + all documented chunks — reflected below. Plan of
record: `~/.claude/plans/mighty-swimming-falcon.md`.

## File layout (post-sweep)

- `formats/common/` — everything format-agnostic, reused by M2/ADT/... later:
  `fourcc.hpp`, `annotations.hpp` (chunk/since/until/optional/header/container/
  repeats — the annotation vocabulary stays snake_case), `chunk.hpp`
  (ChunkExtras, ChunkedFile, ChunkBlob, Repeated, UnknownChunk, JournalEntry),
  `string_block.hpp` (StringBlock), `serializer.hpp` (engine), `flags.hpp`
  (has_flag), `types.hpp` (wire math/color primitives).
- `formats/wmo/` — `data_structs.hpp` (wire structs + flag enums + version
  boundary constants; no "wmo_" prefix in filenames, the directory carries it),
  `root.hpp` (WMORoot), `group.hpp` (WMOGroupBody, WMOGroup), `wmo.hpp` (WMO
  assembly + wmo_versions + welded aliases + extern templates), `wmo.cpp`.
- Type naming: PascalCase classes everywhere (StringBlock, ChunkExtras,
  Repeated); wire structs keep the client's canonical spellings (SMOHeader,
  CAaBspNode); **WMO keeps its acronym** (WMORoot, WMOWotlk — not WmoRoot).
  Flag namespaces became welded scoped enums (GroupFlags, MaterialFlags,
  HeaderFlags, PolyFlags, DoodadFlags, GroupFlags2) with plain-Doxygen
  enumerator comments (welder doc() does not cover enumerators — no
  duplication); wire fields stay plain ints (combined bit values are not valid
  enumerators) tested via `has_flag(value, GroupFlags::x)`.

## Core design

- **Version axis**: `ClientVersion` as NTTP (`WMO<versions::wotlk>`). Layout
  changes = constrained partial specializations (`requires (V < boundary)`);
  chunk-set changes = `since()`/`until()` annotations on entity members.
  Boundary constants live beside the wire structs in data_structs.hpp
  (wmo_trans_batch_data 4.0, wmo_ambient_override 6.0, wmo_legion/
  wmo_batch_large_material 7.0, wmo_uv_animation 7.3, wmo_light_sets 8.1.27826,
  wmo_fdid_refs 8.1.28186, wmo_volumes 8.3, wmo_sl_extensions 9.0,
  wmo_light_extensions 9.1, wmo_split_groups 9.2, wmo_query_faces 10.0,
  wmo_m3_materials 11.0, wmo_portal_extras 11.1).
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
  Undocumented/offset-based layouts stay ChunkBlob (MLIQ, MOTA, MDDL, MPVD,
  MOPB, MOLS, MOMX, MOM3). v14-alpha-only chunks (MOLM/MOLD/MOIN/old-MOLV/
  MPB*) are deliberately unmodeled; MLSO/MOS2 are binary-only; MFOB (12.1)
  postdates the range.
- **read()/write() are entity METHODS**: `ChunkedFile<Derived>` (CRTP mixin
  over ChunkExtras, chunk.hpp) declares them; definitions live in
  serializer.hpp; the engine is `detail::read_entity`/`detail::write_entity`
  (no more free `formats::read<E>`/`write` wrappers). The ChunkedFile bases
  are extern-templated in wowlib::formats (an explicit instantiation must sit
  in the template's enclosing namespace) so serializer expansion stays in
  wmo.cpp.
- **Byte-perfect round-trip is a tested guarantee**: reads are chunk-order
  independent (fourcc dispatch); every encounter lands in the journal; writes
  replay it; unknown/duplicate/capacity-overflow chunks are preserved verbatim;
  <8 trailing stray bytes kept. Fresh entities write declaration order.
- **Member kinds are type-driven** (serializer.hpp dispatch): trivially-
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
- **Conversion** (convert.hpp): unchanged; supported_versions<wmo::WMO> now
  lists all 11. No real steps yet.

## Reflection findings (gcc 16.1, `-freflection`)

Canary: `tests/unit/test_reflection_spike.cpp` — keep compiling. Everything
from milestone 1 still holds (annotations with ClientVersion payloads read
through instantiations; `template for` + splicing; constrained partial
specializations stay trivially copyable). New gotchas from the sweep:
- Annotations are NOT valid in the enumerator position wowlib wanted
  (`[[=doc]] name = 0x1` doesn't parse; the legal trailing position is
  pointless since welder doc() ignores enumerators) → plain Doxygen comments.
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
- Per-version classes weld through namespace-scope aliases (WMOWotlk, ...,
  WMOTheWarWithin — 6 templates x 11 versions, X-macro-generated), flat in
  `wowlib::formats`; version-invariant wire structs weld in `formats.wmo`
  under their client names (SMOMaterial verbatim now, not SmoMaterial).
- **Factories are reflection-generated**: `template for` over
  Expansion enumerators calls `def_wmo_factories<X>` which defs the
  load_wmo/convert_wmo overload pair with consteval-built `nb::sig` strings
  (`define_static_string`); a new Expansion enumerator grows its overloads
  automatically. No load catch-all anymore (every enumerator is instantiated;
  a static_assert enforces coverage); convert_wmo keeps its cross-version
  NotImplemented catch-all. mypy narrowing verified via stubs
  (Literal[Expansion.X] -> WMOX).
- Entity read()/write() methods weld into every entity (bytes in/out through
  the span/FileBuffer casters) — byte-perfect rewrites work from Python
  (probed) and Lua.
- check.py: updated names (FileDataID, WMO*, SMO*), StringBlock Entry
  attributes, flag-enum check; the "unbuilt expansion raises" check became a
  per-expansion dispatch check (WMOCata). check.lua grew a formats block.

## Known issues / deferred

- **Pre-existing (NOT formats)**: `bindings.python`/`bindings.lua` ctest fail
  on `FileSystemSettings` keyword defaults — the uncommitted welder pin bump
  (57e7747 → 19253c7) lost/changed the NSDMI-defaults synthesis. Everything
  before that check (incl. all formats checks + mypy narrowing) passes; the
  real-client formats path was verified with a manual full-arity probe. Fix
  belongs in welder.
- Deferred: M2/ADT/WDT; offset-based intra-chunk data (MLIQ/MOTA/MDDL/M2Array
  — planned as an `offset_view` member kind); real convert steps; Repeated<>
  bindings (sequence protocol); zero-copy/ndarray vector exposure for Blender;
  integration coverage for the 9 versions without local clients.

## Adding a new format (the recipe)

1. `formats/<fmt>/data_structs.hpp`: wire structs (trivially copyable,
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
5. Bindings: extend the factory `template for` (or add a new def_* family),
   stub OUTPUT entry if a new submodule.
