# Python / Lua bindings (welder)

Read when: touching `bindings/`, adding welded types, or debugging binding builds.

## Shape
- `bindings/python/wowlib_module.cpp` — nanobind rod, `WELDER_MODULE(wowlib, nanobind,
  welder<rods::nanobind::rod<>, wowlib_python_naming>)`. Module = namespace `wowlib`;
  every annotated entity in the umbrella header is welded automatically.
  `wowlib_python_naming` (defined in the TU) = snake_case base + VERBATIM
  class/enum/enumerator transforms — wowlib type names are client-canonical
  (SMOHeader, WMORoot, FileDataID) and welder's pep8 CapWords normalization
  would corrupt the acronyms (SmoHeader, WmoRoot, FileDataId).
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
  means converting its doxygen docs back into welder::doc annotations.
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
- **Properties**: `[[=welder::getter]]` / `[[=welder::setter]]` (welder 19253c7+)
  bind accessor methods as properties in BOTH Python and Lua (nanobind
  def_prop_ro/rw, LuaBridge3 addProperty; lone getter = read-only). In use:
  `FileSystem::kind`, `ClientVersion::storage_kind` — attribute access, no `()`,
  in both languages. Name derivation strips a leading get/set word after styling;
  don't also add `weld_as` on an accessor (diagnosed — the mark's optional name
  argument is the rename tool). No `returns()` on getters; fold it into `doc`.
- `bindings/lua/wowlib_module.cpp` — LuaBridge3 rod, snake_case naming.
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
  discovered automatically) — but the expected files must still be listed in
  `OUTPUT` for the build graph; extend the list when adding a namespace. Output:
  `bindings/python/stubs/wowlib/{__init__,fs,versions}.pyi`.

## Result<T> / std::expected translation
welder has no expected support; both TUs teach their framework:
- nanobind: `type_caster<wowlib::Result<T>>` (bindings/python/result_casters.hpp)
  unwraps success, throws `wowlib::result_error` on failure. The module glue
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
- LuaBridge3: `Stack<wowlib::Result<T>>` push-by-VALUE (several payloads are
  move-only — a const& push forces a deleted copy ctor deep in Userdata.h) that
  throws std::runtime_error; LuaBridge3 converts it to a lua error at the call
  boundary. `Result<void>` pushes `true`.
- Also taught: `FileBuffer`/`std::span<const std::byte>` <-> Python `bytes` / Lua
  strings. fs::path + optional are native in both (nanobind/stl/filesystem.h;
  LuaBridge3 detail/Stack.h — do NOT add own specializations, they clash).
- The bindability gate keys on caster presence per rod (`is_base_caster_v` /
  `IsUserdata`), so including the caster headers before WELDER_MODULE is what
  satisfies it — no trust_bindable hatches needed.

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
- Preset `gcc16-bindings` (WOWLIB_BUILD_PYTHON + WOWLIB_BUILD_LUA).
- Python: project venv `.venv` (uv venv --python 3.13 && uv pip install -p .venv
  nanobind); stable ABI (abi3) => `wowlib.abi3.so`, loads across minors and the
  GCC/MSVC boundary (Blender). Stub: `wowlib_pyi` target -> bindings/python/wowlib.pyi
  via nanobind's stubgen.
- Lua: brew lua@5.4 headers; LuaBridge3 FetchContent'd by welder; `wowlib.so`
  (bare name) + `luaopen_wowlib`, lua_* resolved from host interpreter.
- ctest names: `bindings.python`, `bindings.lua` (smoke checks; real-client parts
  gate on WOWLIB_TEST_CLIENTS_DIR).