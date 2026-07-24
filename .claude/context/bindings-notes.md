# Python / Lua bindings (welder)

Read when: touching `bindings/`, adding welded types, or debugging binding builds.

## Shape
- **Python bindings are split into concern-scoped TUs** (2026-07-20; was one giant
  wowlib_module.cpp) that mirror the library layout. All glue lives in a top-level
  `namespace wowlib_py` — deliberately NOT nested in `wowlib`, or welder's module
  walk (which enumerates `members_of(^^wowlib)`) would try to bind it as a
  submodule. Files under `bindings/python/`:
  - `wowlib_module.cpp` — the skeleton: includes the casters (satisfies welder's
    bindability gate), runs `WELDER_MODULE(wowlib, nanobind, welder<rods::nanobind::
    rod<>, wowlib_py::wowlib_python_naming>)`, then calls `register_errors` /
    `formats::wmo::register_facade` / `fs::register_filesystem_protocol` from the
    body. Keeps ALL the `<nanobind/stl/*.h>` casters (array/filesystem/optional/
    pair/string/string_view/vector) — the walk needs them for welded members.
  - `naming.hpp` — `wowlib_python_naming` = snake_case base + VERBATIM
    class/enum/enumerator transforms (client-canonical acronyms: SMOHeader,
    WMORoot, FileDataID; pep8 CapWords would corrupt them → SmoHeader/FileDataId).
  - `errors.hpp/.cpp` — `register_errors`: the reflection-generated exception
    hierarchy + the Result-error translator.
  - `buffers.hpp/.cpp` — `to_buffer`/`to_pybytes` (Python byte source/sink ↔
    FileBuffer).
  - `facade.hpp` — **generic** versioned-facade machinery (header-only templates):
    `expansion_enumerators`, `persist`, `concrete_name`, `make_one`,
    `def_for_version`. Format-agnostic — templated on the family template `F`, so
    ADT/M2 reuse it verbatim; only the per-format read/write/convert lives in a
    format TU.
  - `formats/wmo.hpp/.cpp` — `register_facade`: `for_version` on every WMO family
    base + the read/write/convert surface on WMOBase (mirrors `src/.../formats/wmo/`).
  - `fs.hpp/.cpp` — `register_filesystem_protocol`: the `__enter__`/`__exit__`
    dunders.
  - `result_casters.hpp`, `stub_patterns.nb`, `check.py` unchanged in role. (The
    AnyX unions are REAL runtime aliases now — `def_any_alias`, see below — but the
    PATTERN_FILE stays to spell them correctly in the stubs.)
  - CMake: ONE `nanobind_add_module(wowlib_py STABLE_ABI ...)` lists all the `.cpp`s;
    `target_include_directories(wowlib_py PRIVATE .../python)` makes quoted includes
    (`"formats/wmo.hpp"`, `"result_casters.hpp"`) resolve against the python/ root.
  - Glue functions are the split boundary; a new format adds `formats/<fmt>.{hpp,cpp}`
    + one `register_facade` call in the module body + one source line in CMake.
    **Doc policy inside the binding TUs: plain Doxygen** (these are unwelded glue, so
    `welder::doc` does NOT apply — file `@file` docstring + `@brief`/`@param` on
    every helper).
- **Welded surface is deliberately minimal**: FileSystem + FileSystemSettings +
  the helper types their signatures need (FileKey, FileDataID, ClientVersion,
  StorageKind, Locale, versions constants). FileKey is the GENERIC identity for
  version-independent tools: read_file/exists take str | FileDataId | FileKey
  (merged overloads; the str/fdid forms delegate to the FileKey one in C++),
  and resolve(FileKey) fills the missing half via the listfile. ProjectDirectory,
  CsvListfile(+Options), Error/ErrorCode are NOT welded — the facade takes plain
  paths in settings. check.py/check.lua assert the absences.
- **Doc policy**: `welder::doc` ONLY on welded entities; unwelded C++ documents
  itself with plain doxygen comments (user rule). Adding a weld mark to a class
  means converting its doxygen docs back into welder::doc annotations. A welded
  entity carries NO leading `/** */` doxygen block AND welder annotations — that
  is duplication: welder's Doxygen filter promotes doc/param/returns/tparam to
  the C++ reference, so the annotation is the single source (fold any unique
  detail from the old doxygen into the doc() text, then delete it). Enumerator
  docs stay plain `/**< */` (welder doc() does not cover enumerators). Welded
  class/struct/enum definitions use the compact `struct [[ …attrs… ]] Name` form
  (attribute between keyword and name), per FileSystemSettings — not the
  four-line struct/`[[`/`]]`/Name layout.
- **Immutable settings pattern** (FileSystemSettings): a const-member AGGREGATE
  with NSDMIs on everything after `version` — C++ keeps designated init (no
  user ctor!), Python gets welder's synthesized field ctor with real keyword
  defaults (welded-type defaults spell `...` in stubs), Lua gets one ctor arity
  per omissible tail. GOTCHA: those defaults convert eagerly at registration,
  and the module walk is declaration-ordered — wowlib.hpp must NOT pre-open
  `namespace fs` before the core types (FileDataID) or import dies with
  std::bad_cast; the umbrella opens fs between the core and fs includes.
- **Scripting-only members**: methods that must exist for Python/Lua but NOT in
  the C++ public interface (FileSystem::close, is_open) are `protected` +
  `=welder::policy::weld_protected` on the class. Python's context-manager
  dunders are attached in the module glue (cpp_function + is_method; __exit__
  must take nb::args — nb::handle parameters reject None).
- **Opaque containers — vectors bind by reference** (welder 04fded7+, 2026-07-22):
  the opaque-container generator rod reflects `namespace wowlib` and emits
  `wowlib.opaque.hpp` (`NB_MAKE_OPAQUE` decls + welded `Vector*` aliases), so every
  WMO chunk `std::vector` binds via `nb::bind_vector` — live mutation, `append`,
  zero-copy NumPy — not the old copy-to-`list` caster. Wiring: gen TU
  `python/opaque_gen.cpp` (`WELDER_OPAQUE_CONTAINERS_MAIN(wowlib)`) built+run by
  `welder_generate_opaque_containers()` (its CMake helper lives on
  `${welder_SOURCE_DIR}/cmake` — re-add to CMAKE_MODULE_PATH + `include()` it in
  bindings/CMakeLists; the gen exe links `wowlib` PRIVATE for its PUBLIC
  `-freflection`+includes; storm/casc are PRIVATE so they're not pulled). The module
  TU includes the generated header AFTER `nanobind/module.hpp` (defines
  WELDER_OPAQUE) and BEFORE WELDER_MODULE. `<nanobind/stl/vector.h>` STAYS —
  per-type NB_MAKE_OPAQUE suppresses the copy caster only for the opaque types.
  Runtime: base-less POD-struct element → `__array_interface__` (numpy-free
  structured dtype); scalar element → `__array__` (nb::ndarray, needs numpy). ALL WMO
  vectors are opaque, including the NTTP-versioned `WMO::groups`
  (`vector<WMOGroup<V>>`) and `WMOGroupBody::batches` (`vector<SMOBatch<V>>`). Those
  two get reference semantics but NO `__array_interface__`: they carry an empty facade
  base (EBO), and welder's POD-array eligibility skips based types — fine, they aren't
  the hot geometry path.
- **Container wrapper NAMES come from a `transform_opaque_container` style hook**
  (welder 31b0801+, `WELDER_OPAQUE_CONTAINERS_MAIN_STYLED`), defined in
  `opaque_gen.cpp` as `wowlib_py::wowlib_opaque_naming`: `Vector` + the element's
  VERBATIM identifier (client acronyms intact — `VectorSMOMaterial`, `VectorC3Vector`,
  matching how the element itself binds; welder's default would restyle/qualify to
  `VectorWmoChunksSmoMaterial`). A versioned entity template (sole `ClientVersion` NTTP
  arg) suffixes the Expansion spelling → `VectorWMOGroupWotlk`, `VectorSMOBatchTbc`.
  Scalars/strings/maps fall through to welder's `derive_name` (`VectorFloat`,
  `VectorShortUnsignedInt`). GOTCHA: an NTTP template arg's `type_of` is `const T`, so
  detect it with `remove_cv(type_of(arg)) == ^^ClientVersion`; `extract<ClientVersion>`
  then feeds `to_expansion`. The `Vector*` aliases weld at top-level `namespace
  wowlib`, so they land in `wowlib/__init__.pyi` — no new stub OUTPUT entry.
- **All WMO vectors carry `[[=welder::mark::no_reassign]]`** (welder 31b0801+): the
  property binds read-only (getter only — no setter) so the whole attribute can't be
  rebound (`wmo.groups = ...` raises AttributeError), but the opaque object is still a
  live reference, so in-place mutation writes through (`body.vertices.append(...)`,
  `body.vertices[0] = ...`). Applied to every bound `std::vector` member in
  root.hpp/group/group.hpp/wmo.hpp; the excluded `Repeated<>` and the `ChunkBlob`
  members are NOT marked.
- **Enumerator docs are welder::doc now** (welder 04fded7+): the flag/enum members
  use `Name [[=welder::doc("…")]] = value` (annotation AFTER the enumerator name, not
  before) — welder folds documented enumerators into the enum's class docstring as an
  `Attributes:` section (stubgen carries it). Supersedes the old plain-`/**< */`
  policy for enumerators; struct-member `/**< */` doxygen is unchanged.
- **Properties**: `[[=welder::getter]]` / `[[=welder::setter]]` (welder 19253c7+)
  bind accessor methods as properties in BOTH Python and Lua (nanobind
  def_prop_ro/rw, LuaBridge3 addProperty; lone getter = read-only). In use:
  `FileSystem::kind`, `ClientVersion::storage_kind` — attribute access, no `()`,
  in both languages. Name derivation strips a leading get/set word after styling;
  don't also add `weld_as` on an accessor (diagnosed — the mark's optional name
  argument is the rename tool). No `returns()` on getters; fold it into `doc`.
- **Lua bindings are DEFERRED** (2026-07-20): `bindings/lua/`, the `wowlib_lua`
  target, `WOWLIB_BUILD_LUA` and the Dependencies.cmake LuaBridge3 block are all
  removed until the library is feature-complete (lower priority, distracting). The
  `lang::lua` / `mark::only(lang::lua)` welds STAY in the sources — Lua is planned,
  so keep pretending it binds; this file's Lua design notes describe the intended
  shape for when it returns (was: LuaBridge3 rod, snake_case naming everything).
- **C++ namespaces arrive as submodules** in both languages: `wowlib.fs`,
  `wowlib.versions` (constants, no `()`); `wowlib::fs::detail` is unannotated and
  never surfaces.
- Name reshaping: Python binds classes/enums/enumerators VERBATIM
  (`FileDataID` stays `FileDataID`, `SMOHeader` stays `SMOHeader`) and
  snake_cases callables/data; Lua snake_case reshapes EVERYTHING
  (`ClientVersion -> client_version`, `wowlib.fs.file_system`,
  `StorageKind.Mpq -> storage_kind.mpq`, `Locale.enUS -> locale.en_us`,
  `WMORoot<wotlk> alias -> wmo_root_wotlk`).
- Overloads merge under one name (Python `read_file(str|FileDataId)`) — never use
  weld_as to split them.
- Stubs: ONE `nanobind_add_stub` with `RECURSIVE` + `OUTPUT_PATH` (submodules
  discovered automatically) + a small `PATTERN_FILE` (AnyX union spellings) — but the
  expected files must still be listed in `OUTPUT` for the build graph; extend the list
  when adding a namespace. Output:
  `stubs/wowlib/{__init__,fs,versions}.pyi` + `stubs/wowlib/formats/__init__.pyi`
  + `stubs/wowlib/formats/wmo/__init__.pyi` + `wmo/root/{__init__,chunks}.pyi` +
  `wmo/group/{__init__,chunks}.pyi` (wmo is a package whose root/ and group/
  submodules are themselves packages, each with a nested `chunks` submodule —
  extend OUTPUT when a submodule is added).
- **The versioned-format facade is native** (welded C++ bases → real
  inheritance/isinstance; for_version/read/write/convert are nb::sig merged
  overloads on the base; see formats-architecture.md). **There is NO stub
  post-processor** — the earlier facade_stub.py was deleted (user rejected it as
  unmaintainable). The `AnyX` unions are now bound at import as real
  `types.UnionType` objects (`def_any_alias`, facade.hpp) — importable at runtime
  (`from wowlib.formats.wmo import AnyWMO`) and isinstance-capable. The only
  declarative piece left is `stub_patterns.nb` (PATTERN_FILE): nanobind 2.13's
  stubgen mis-renders a UnionType as the invalid `types.UnionType[...]` subscript,
  so a per-member pattern suppresses that and writes the correct
  `AnyX = C0 | ...`. stubgen still auto-imports the typing/collections.abc/wowlib.fs
  names the nb::sig strings use, so no import lines are needed in the pattern file.

## Nested opaque containers (welder 5bf54fa, 2026-07-24)

- The opaque generator now opens NESTED container chains
  (vector<vector<T>>, map<K, vector<T>>) by reference: eligibility recurses,
  collection adds every level, aliases render depth-major so inner wrappers
  weld before the outers that name them. Motivating case: Legion+ M2 track
  timestamps/values (vector<vector<u32>> / vector<vector<T>>) previously
  degraded to copied Python lists of inner wrappers.
- Upstream 91e224d (same day) fixed element ACCESS: nanobind bind_vector/
  bind_map now pass rv_policy::reference_internal (the automatic_reference
  default COPIES lvalue-reference elements out of __getitem__/__iter__), so
  v[i].field = x and nested row mutation write through. Standard caveat:
  growing/clearing a container invalidates outstanding element references.
- wowlib names them via welder's derived fallback (VectorVectorUnsignedInt,
  VectorVectorC3Vector, ...) — the wowlib_opaque_naming hook only styles
  plain welded-class and versioned-entity elements.

## Result<T> / std::expected translation
welder has no expected support; each rod teaches its framework (Python live; the
Lua notes below describe the intended shape for when that target returns):
- nanobind: `type_caster<wowlib::Result<T>>` (bindings/python/result_casters.hpp)
  unwraps success, throws `wowlib::result_error` on failure. `errors.cpp`
  builds a **reflection-generated exception hierarchy**: `wowlib.Error` base +
  one class per ErrorCode enumerator (template for over enumerators_of,
  PyErr_NewExceptionWithDoc — limited-API safe), some with curated extra builtin
  bases (FileNotFound→FileNotFoundError, IoError/ListfileIoError→OSError,
  NotImplemented→NotImplementedError). A register_exception_translator raises the
  per-code class with the PLAIN message (no code prefix — the class is the code)
  and sets `code` (str spelling) + `native_error` (int) attributes. New ErrorCode
  enumerators grow new exception classes automatically; extend builtin_base() when
  a new code has a natural Python builtin. nanobind's stubgen picks the classes up
  (they land in __init__.pyi with bases and docstrings).
- LuaBridge3 (deferred): `Stack<wowlib::Result<T>>` push-by-VALUE (several payloads are
  move-only — a const& push forces a deleted copy ctor deep in Userdata.h) that
  throws std::runtime_error; LuaBridge3 converts it to a lua error at the call
  boundary. `Result<void>` pushes `true`.
- Also taught: `FileBuffer`/`std::span<const std::byte>` <-> Python `bytes` / Lua
  strings. fs::path + optional are native in both (nanobind/stl/filesystem.h;
  LuaBridge3 detail/Stack.h — do NOT add own specializations, they clash).
- The bindability gate keys on caster presence per rod (`is_base_caster_v` /
  `IsUserdata`), so including the caster headers before WELDER_MODULE is what
  satisfies it — no trust_bindable hatches needed.

## Gotchas learned (M2 facade, 2026-07-24)
- **The walk completes `absent<Trait>`'s template argument** even for versions a
  slot leaves inactive — a version-slot TRAIT template must therefore be valid to
  instantiate for EVERY version. Traits whose members are constrained templates
  (AssemblySkins' Skin, AssemblyLegion's M2File/Skeleton) use an EMPTY
  unconstrained primary + a constrained partial specialization carrying the
  members (m2.hpp detail::Assembly*).
- **Defaulted `operator==` on an unwelded base needs `mark::exclude`**: it
  flattens into the welded derived class and welder binds it, but its parameter
  type is the unwelded base → bindability assert. Excluded on ChunkExtras,
  ChunkedFile, OffsetBase, OffsetFile, absent<>, and every M2 trait struct.
  Welded classes keep their own defaulted == (that's Python `__eq__`).
- **A welded entity's whole method surface must LINK**: the skel/companion
  payload offset entities only ever ran their context read/write overloads in
  the library, so the plain `read(span)`/`write()` symbols welder binds were
  undefined at link — the OffsetFile<payload> instantiations are now explicit
  (m2.hpp externs + skel.cpp).
- **formats/opaque_extra.hpp is GONE**: M2's FBlock<C2Vector> made the generator
  discover `std::vector<C2Vector>` directly, so the manual stopgap became a
  duplicate NB_MAKE_OPAQUE. WMO texcoords still bind zero-copy through the
  generated wrapper.
- **Subset families** (Skin WotLK+, M2File/Skeleton Legion+): facade.hpp's
  `family_has<F, X>` concept guards every expansion walk (for_version overloads,
  the runtime fallback, def_any_alias — now templated on F; AnyX folds only the
  existing eras). def_any_alias's old untyped signature is gone — WMO call sites
  pass the family template.
- **M2 exposes NO buffer-parse read on the Python base** (unlike WMO): an offset
  model pulls satellites during read, so only the (FileSystem, FileKey) form is
  complete; a span+satellites parse API is a follow-up. Skeleton gets its own
  read/write verbs on SkeletonBase (shared .skel files serialize standalone).
- The M2 welded surface is BIG (~350 classes: 9 track combos + M2TrackBase and
  ~14 record families × versions, via m2.hpp's records alias X-macros — ordered
  tracks → records → satellites → assembly because welded NSDMI defaults convert
  eagerly at registration). wowlib_module.cpp now takes ~8-10 min to compile.

## Gotchas learned
- Types with only excluded/private ctors: welder static-asserts unless a ctor is
  explicitly `mark::exclude`d (factory-only surface must be declared, not
  implied) — see FileSystem's excluded variant ctor, ClientFileSystem's excluded
  main ctor.
- `find_package(Python)` results are directory-scoped: bindings/CMakeLists must
  find_package itself even though welder already did.
- `CMAKE_CXX_SCAN_FOR_MODULES OFF` globally — p1689 scanning breaks nanobind's
  runtime TUs and LuaBridge3 macro reliance.
- LuaBridge3 ships Stack for fs::path, optional, span, std::expected
  (StdExpected.h, NOT auto-included; our Result spec is more specialized and
  wins regardless).

## Build
- Preset `gcc16-bindings` (WOWLIB_BUILD_PYTHON only; Lua deferred).
- Python: project venv `.venv` (uv venv --python 3.13 && uv pip install -p .venv
  nanobind); stable ABI (abi3) => `wowlib.abi3.so`, loads across minors and the
  GCC/MSVC boundary (Blender). Stub: `wowlib_pyi` target -> bindings/python/wowlib.pyi
  via nanobind's stubgen.
- Lua (when reinstated): brew lua@5.4 headers; LuaBridge3 FetchContent'd by welder;
  `wowlib.so` (bare name) + `luaopen_wowlib`, lua_* resolved from host interpreter.
- ctest name: `bindings.python` (smoke check; real-client parts gate on
  WOWLIB_TEST_CLIENTS_DIR). `bindings.lua` returns with the Lua target.

## Type casters (`bindings/python/*_caster*.hpp`, included before the WELDER walk)
A self-contained nanobind `type_caster` (built with `NB_TYPE_CASTER`, so it does
NOT derive from the registration base) makes `has_native_caster<T>` true, which
satisfies welder's bindability gate automatically AND names the type in the `.pyi`
stubs — no weld/trust mark (welder "Trust & casters", option 3). Casters live in
`result_casters.hpp` (Result<T>, FileBuffer, byte spans) and
`formats/repeated_caster.hpp`, all `#include`d in `wowlib_module.cpp` BEFORE the
`WELDER_MODULE` macro (that ordering is what the gate reads). Limited C API only,
for stable-ABI validity.
- **`Repeated<T,N>`** (chunk.hpp fixed-capacity slot container; WMO MOTV texcoords,
  MOCV vertex_colors) → binds as a Python `list` of its filled slots, **by value**
  (read copies out; assign fills fresh slots via `push()`, rejecting `len > N` with
  TypeError). This is why those two members are no longer `mark::exclude`; they
  carry `mark::only(lang::py)` since only nanobind has the caster (a future Lua
  build needs its own, or they stay py-only). Inner `std::vector<E>` renders opaque
  zero-copy iff `VectorE` was generated (vertex_colors → `list[VectorCImVector]`),
  else a plain by-copy `list[E]` (texcoords → `list[list[C2Vector]]`, since nothing
  else uses `std::vector<C2Vector>` so `VectorC2Vector` isn't generated).