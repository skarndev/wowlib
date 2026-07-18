# Python / Lua bindings (welder)

Read when: touching `bindings/`, adding welded types, or debugging binding builds.

## Shape
- `bindings/python/wowlib_module.cpp` — nanobind rod, `WELDER_MODULE(wowlib, nanobind,
  welder<rods::nanobind::rod<>, rods::python::pep8>)`. Module = namespace `wowlib`;
  every annotated entity in the umbrella header is welded automatically.
- `bindings/lua/wowlib_module.cpp` — LuaBridge3 rod, snake_case naming.
- **C++ namespaces arrive as submodules** in both languages: `wowlib.fs`,
  `wowlib.versions` (constants, no `()`); `wowlib::fs::detail` is unannotated and
  never surfaces.
- Name reshaping: Python pep8 keeps CapWords types but `FileDataID -> FileDataId`;
  Lua snake_case reshapes EVERYTHING (`ClientVersion -> client_version`,
  `wowlib.fs.file_system`, `StorageKind.Mpq -> storage_kind.mpq`,
  `Locale.enUS -> locale.en_us`).
- Overloads merge under one name (Python `read_file(str|FileDataId)`) — never use
  weld_as to split them.
- Stubs: ONE `nanobind_add_stub` with `RECURSIVE` + `OUTPUT_PATH` (submodules
  discovered automatically) — but the expected files must still be listed in
  `OUTPUT` for the build graph; extend the list when adding a namespace. Output:
  `bindings/python/stubs/wowlib/{__init__,fs,versions}.pyi`.

## Result<T> / std::expected translation
welder has no expected support; both TUs teach their framework:
- nanobind: `type_caster<wowlib::Result<T>>` (bindings/python/result_casters.hpp)
  unwraps success, throws `wowlib::result_error` on failure; the module glue
  registers `nb::exception<result_error>(module, "WowlibError")`. Message =
  `"<ErrorCode>: <message>"` (ErrorCode spelled via reflection to_string).
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