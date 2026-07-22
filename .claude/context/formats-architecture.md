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
- `formats/wmo/` — **the directory tree mirrors the namespace tree** (user rule,
  2026-07-20), so each sub-namespace is a subdirectory and the four submodules
  (`wmo.root`, `wmo.group`, `wmo.chunks`, `wmo.group_chunks`) produce small,
  clean per-file `.pyi`s:
  - `boundaries.hpp` (`wmo`) — version-boundary constants + wmo_version_v17.
  - `wmo.hpp`/`wmo.cpp` (`wmo`) — the WMO assembly + WMOBase, wmo_versions, the
    per-namespace welded aliases (X-macro) and extern/instantiations.
  - `root/root.hpp` (`wmo::root`) — WMORoot + WMORootBase.
  - `group/group.hpp` (`wmo::group`) — WMOGroup/WMOGroupBody + their bases.
  - `chunks/` (`wmo::chunks`) — root wire structs split by chunk family:
    `header.hpp material.hpp structure.hpp light.hpp doodad.hpp environment.hpp`.
  - `group_chunks/` (`wmo::group_chunks`) — group wire structs +
    SMOGroupHeader/SMOBatch bases: `header.hpp geometry.hpp light.hpp`.
  (data_structs.hpp — a single 1247-line file — was split into these; "avoid
  extremely long files, split logically" is a user rule.)
- Type naming: PascalCase classes everywhere (StringBlock, ChunkExtras,
  Repeated); wire structs keep the client's canonical spellings (SMOHeader,
  CAaBspNode); **WMO keeps its acronym** (WMORoot, WMOWotlk — not WmoRoot).
  Flag namespaces became welded scoped enums (GroupFlags, MaterialFlags,
  HeaderFlags, PolyFlags, DoodadFlags, GroupFlags2) whose enumerators carry
  `Name [[=welder::doc("…")]] = value` (welder 04fded7+ documents enumerators —
  folded into the enum class docstring's `Attributes:` section; annotation goes
  AFTER the name); wire fields stay plain ints (combined bit values are not valid
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
- **Conversion** (convert.hpp): supported_versions<wmo::WMO> lists all 11.
  No real steps yet. `has_convert_path<E, From, To>()` (consteval) reports
  whether the full step ladder for a pair exists — runtime-dispatching callers
  (Python factories) use it to degrade a missing ladder to a NotImplemented
  Result instead of convert()'s static_assert.

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
    copyable wire structs — `WMOGroupHeaderBase`/`WMOBatchBase`). The per-version
    template *inherits* it (`WMO<V> : WMOBase`; `WMORoot<V> : ChunkedFile<...>,
    WMORootBase` — ChunkedFile is a non-welded mixin so nanobind's single-welded-
    base rule holds). welder registers the base natively → `class WMOWotlk(WMO)`
    in the stub AND real `isinstance`/`issubclass`, ZERO glue. Wire-struct bases
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
  - **AnyX unions are the ONE thing stubgen can't synthesize** → the declarative
    stub **PATTERN_FILE** (`__prefix__:` per submodule) emits `AnyWMO = C0 | ...`;
    stubgen auto-imports the typing/collections.abc/wowlib.fs names the sigs use.
    A new expansion edits the unions there (one more coupling point with
    wmo_versions + the X-macro).
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
- Deferred: M2/ADT/WDT; offset-based intra-chunk data (MLIQ/MOTA/MDDL/M2Array
  — planned as an `offset_view` member kind); real convert steps; Repeated<>
  bindings (sequence protocol; texcoords/vertex_colors still mark::exclude'd);
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
5. Bindings: add a `def_family<F>(wmo, "Name", doc)` call per version-differing
   family and (for entities with verbs) a `def_wmo_verbs`-style glue; a new
   Expansion enumerator grows every facade automatically (template for). Extend
   facade_stub.py's FAMILIES if a new family; stub OUTPUT entry if a new submodule.
6. wowlib.hpp: pre-declare the format's doc'd namespace BELOW the
   formats/common includes (namespace position = submodule weld order, and
   NSDMI defaults of common types convert eagerly at registration — see Known
   issues, 2026-07-20).
