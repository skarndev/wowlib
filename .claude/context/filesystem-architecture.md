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
- open()/close() are NOT thread-safe; initialize before sharing.

## Documentation / annotation policy (user-set convention)
Public bound API → welder P3394 annotations (`[[=welder::weld(py, lua)]]`,
`=welder::doc/returns/tparam/weld_as/mark::include...`, vocabulary from
`<welder/vocabulary.hpp>`). Internal, never-bound entities (concepts, chain
tables, path utils, backend internals) → plain Doxygen `/** */` (autobrief, no
`///`). Classes with lock members etc. use `policy::opt_in` + `mark::include` per
method. `-freflection` is PUBLIC on the wowlib target (annotations need it in
every including TU); welder checks but does not propagate the flag itself.