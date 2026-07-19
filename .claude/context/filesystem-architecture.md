# Client filesystem gateway architecture

Read when: touching anything under `src/wowlib/fs/` or the core types it builds on.

## Layers (static polymorphism, no virtuals)

```
FileSystem (fs/filesystem.hpp)          — runtime facade, std::variant dispatch;
  │                                       THE welder binding surface
  └─ ClientFileSystem<Backend, Listfile> (fs/client_filesystem.hpp)
       ├─ Backend: MpqStorage | CascStorage   (StorageBackend concept)
       ├─ Listfile: CsvListfile | NullListfile (ListfileProvider concept)
       └─ optional ProjectDirectory overlay
```

- Concrete compositions `MpqFileSystem` / `CascFileSystem` are explicit template
  instantiations in `fs/filesystem.cpp` — these are what welder binds; C++ callers
  wanting zero dispatch use them directly (`FileSystem::mpq()/casc()` exposes them).
- `FileKey` carries path and/or FileDataID; `ClientFileSystem::resolve` fills the
  missing half via the listfile. Routing: MPQ = path-addressed (id-only requests
  fail `FdidNotResolvable` unless the listfile maps them); CASC = id-addressed
  (path-only → listfile, then best-effort `CASC_OPEN_BY_NAME`, else
  `PathNotResolvable`).
- Lookup precedence: project directory beats client storage, replicating "ultimate
  patch" semantics.
- Canonical path form everywhere: lowercase + backslash (`core/path.hpp`). Convert
  at boundaries only (CASC wants '/', POSIX disk wants '/').

## Error handling
`Result<T> = std::expected<T, Error>` (`core/error.hpp`); no exceptions in the
core. `Error::native_error` carries GetLastError()/GetCascError(). Bindings
translate to exceptions at the welder layer.

## Thread-safety contracts
- `MpqStorage`: one `std::mutex` per opened archive; probes and reads lock the
  archive they touch (StormLib mutates state on lookups). Reads of different
  archives run in parallel. Upgrade path: per-archive handle pools, only if
  contention is measured.
- `CascStorage`: one mutex around the whole open/size/read/close sequence
  (CascLib handles not documented thread-safe). Upgrade path: storage-handle pool
  (memory-heavy, measure first).
- `CsvListfile` / `ProjectDirectory`: `std::shared_mutex` — shared lookups,
  exclusive mutation. `FdidAllocator` is serialized by its owner's exclusive lock.
- Opening (the static factories) is NOT thread-safe; initialize before sharing.

## Namespaces (review decision 2026-07-18)
- `wowlib` — core vocabulary (FileDataID, FileKey, Error, Result, ClientVersion,
  Locale, StorageKind, path utils, enum_name).
- `wowlib::versions` — last-minor-of-major `inline constexpr ClientVersion`
  CONSTANTS (not factory functions): `versions::wotlk`, no `()`.
- `wowlib::fs` — everything filesystem: storages, listfile providers, overlay,
  ClientFileSystem, FileSystem facade, FileSystemSettings.
- `wowlib::fs::detail` — implementation details (mpq chain tables, FdidAllocator).
  Never welded. Python/Lua modules mirror the namespaces as submodules.

## Documentation / annotation policy (user-set conventions)
- Public bound API → welder P3394 annotations; vocabulary from
  `<welder/vocabulary.hpp>`. Policy: default (automatic) — public members bind,
  no `mark::include` spam; `mark::exclude` only for public members that must not
  bind (unwelded return types, C++-only ctors).
- Argument docs go AFTER the argument (`T name [[=welder::doc("...")]]`), each
  annotated argument on its own line.
- Multiline doc text uses raw string literals starting with a newline
  (`=welder::doc(R"(\n    text...)")`); single-liners stay plain strings.
- Do NOT use weld_as to split overloads — welder merges overloads natively.
- Internal, never-bound entities → Doxygen `/** */` (autobrief), thorough:
  @file block on every header, @tparam/@param/@return everywhere.
- Members: `_` PREFIX for private/protected (`_mtx`, not `mtx_`).
- `-freflection` is PUBLIC on the wowlib target; welder checks but does not
  propagate the flag itself.

## RAII / lifetime (user-set design, 2026-07-19)
Constructed ⇒ open, destroyed ⇒ closed; NO public open()/close() in C++ anywhere.
- Backends: `static Result<MpqStorage> open(Options)` / `CascStorage::open` are
  the only way in (private ctor); private close()/is_open(); destructor and
  move-assignment-onto-open close. The only C++-reachable not-open state is
  moved-from (destroy/assign only). StorageBackend concept spells just the usage
  surface (read_file/exists/kind + movable).
- FileSystem facade: `close()`/`is_open()` are PROTECTED, welded via
  `policy::weld_protected` — scripting languages get deterministic release
  (GC timing isn't control), C++ cannot misuse it. Closed state = monostate
  variant alternative, reachable only via close(); all entry points degrade to
  StorageNotOpen (Python: typed wowlib.StorageNotOpen). kind survives close
  (stored StorageKind, not visited).
- Python context manager: __enter__/__exit__ attached in module glue
  (nanobind cpp_function + is_method; __exit__ takes nb::args — nb::handle
  params reject None). `with FileSystem.open(s) as fs:` scopes the release.
  Lua: fs:close() explicitly; GC (__gc) is the fallback in both languages.