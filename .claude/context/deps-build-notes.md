# Dependencies & build quirks

Read when: touching CMake files, bumping dependency pins, or hitting toolchain
errors.

## Toolchain
- gcc-16 (Homebrew, 16.1.0) ONLY — enforced in `cmake/CompilerSettings.cmake`.
  Configure: `cmake -B build -G Ninja -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16`.
- `-freflection` is applied **PUBLIC** on the wowlib target: public headers carry
  welder P3394 annotations, and gcc-16 gates annotation syntax behind that flag
  (no separate annotation flag). welder checks but never propagates it.
- Tests: `ctest --test-dir build`; integration needs
  `WOWLIB_TEST_CLIENTS_DIR=/Users/skarn/WoWModding/Clients` and
  `WOWLIB_TEST_LISTFILE=/Users/skarn/WoWModding/Listfiles/community-listfile.csv`.

### The gcc Darwin string-literal miscompile (GCC PR 126723) — RESOLVED 2026-08-24
gcc 16.1's Darwin port named string literals with assembler-temporary `L.str.N`
labels (regression from r16-2939, the asan-strings patch — `for_asan` got the
safe lowercase `l`, plain builds the broken `L`). Under
`.subsections_via_symbols` such a label opens no Mach-O atom, so a literal in a
regular section (`__TEXT,__const`, e.g. libstdc++'s to_chars digit table — NOT
`__cstring`, which ld coalesces by content) after a coalescable weak symbol
(the `std::span __v<N>` blobs reflection materializes in every wowlib TU) rode
the weak atom and silently resolved into whichever TU won coalescing:
`std::format("{:016x}")` returned NUL-riddled hex, breaking CASC opens. Both
arches were affected (the rename lives in arch-shared `darwin.cc`).

Filed as **GCC PR 126723** (2026-08-07). Fixed for GCC 17 on master
(r17-3243: all Darwin string labels linker-visible `l.str.*`, matching clang's
aarch64 behavior) and in iains' gcc-16-branch — **Homebrew's gcc 16.2.0 Darwin
diff carries it** (the `for_asan` conditional is deleted outright; `LaC`
anchored constants became linker-visible too). Verified locally on 16.2.0:
a `<format>` TU emits 395 `l.str` / 0 `L.str` beside 81 weak defs.

Consequently the `-B` assembler shim (`cmake/darwin-as-shim/`, renamed
`L.str.*`→`l.str.*`) and its two-TU configure-time probe
(`cmake/DarwinAtomProbe.cmake`) were REMOVED 2026-08-24; CompilerSettings.cmake
now hard-fails a Darwin gcc < 16.2 instead (the corruption is runtime-silent —
never soften that gate to a warning). Full write-up, probe sources, and the
mixed-TU/`-pipe`/`-Wa,-L` landmines: `git log -- cmake/darwin-as-shim`. This
also unblocked the Intel macOS release leg (the probe was aarch64-only asm);
restoring it needs only the matrix entry + the NuGet `osx-x64` RID.

## Pins (FetchContent, cmake/Dependencies.cmake)
| Dep | Tag | Notes |
|---|---|---|
| StormLib | v9.40 | target `storm`; `STORM_SKIP_INSTALL`, `BUILD_SHARED_LIBS=OFF`; links SDK zlib/bzip2 |
| CascLib | 3.0 | target `casc_static`; `CASC_BUILD_STATIC_LIB=ON`, `CASC_BUILD_SHARED_LIB=OFF`, unicode off |
| welder | commit 0a422a4 (main; csharp-core merged, C# rod extracted out-of-tree) | header-only, target `welder::headers`; **must be a pushed commit** (see below) |
| welder-csharp | commit 8d085db (SHARDS shim splitting; CS_FILES wrapper splitting + XML doc file) | the C#/.NET rod as an out-of-tree welder extension; MakeAvailable only under `WOWLIB_BUILD_CSHARP`; uses our already-populated `welder::headers` (declaration order makes our welder pin win) |
| Catch2 | v3.9.1 | `Catch2::Catch2WithMain` + `catch_discover_tests` (extras in module path) |
| stb_dxt | raw-file URL pin at 7023e27 + SHA256 (DOWNLOAD_NO_EXTRACT) | INTERFACE target `stb_dxt`, PRIVATE into wowlib; only formats/blp/blp.cpp includes it (compress-only: BC1-opaque/BC3/BC4/BC5; wowlib decodes itself) |

## Bumping the welder pin (we own welder)
- **Push welder before bumping the wowlib pin.** `FETCHCONTENT_UPDATES_DISCONNECTED
  ON` means FetchContent never fetches after the first populate: if the pinned SHA
  isn't already in `build/<cfg>/_deps/welder-src`, configure hard-fails with
  *"Requested git ref <sha> is not present locally, and not allowed to contact
  remote due to UPDATE_DISCONNECTED"* — and if that SHA was a local-only commit
  that got rebased/re-pushed, even a manual `git fetch origin <sha>` fails with
  *"upload-pack: not our ref"* (GitHub only serves ref-reachable SHAs).
- Recovery when a pin points at an unfetchable SHA: **look in the local welder
  repo first** (`~/WoWModding/Projects/welder`) — the SHA is usually an unpushed
  commit sitting there; push it and `git -C build/<cfg>/_deps/welder-src fetch
  origin`, then rebuild. Do NOT assume the SHA "became" some pushed commit and
  repin downward: pin `57e7747` (2026-07-19, the NSDMI-defaults feature) was
  misdiagnosed as "re-pushed as 19253c7" — but 19253c7 was its PARENT, and the
  downward repin silently dropped the feature (the FileSystemSettings
  keyword-defaults ctest failures blamed on a "welder regression" until
  2026-07-20, when 57e7747 was finally pushed and repinned).

## Configure-time performance (2026-07-18)
- `FETCHCONTENT_UPDATES_DISCONNECTED ON` in Dependencies.cmake: every pin is an
  immutable tag/commit, and without it each reconfigure ran a network `git fetch`
  per dependency (~3 min wall-clock at 17% CPU). No-op reconfigure now ~1.4s.
- Lua/LuaBridge3 is **deferred** (2026-07-20): the target, the binding TU
  (`bindings/lua/`) and the `WOWLIB_BUILD_LUA` knob are removed until the library
  is feature-complete — Lua is lower priority and distracting. The `lang::lua`
  welds STAY in the sources (Lua is planned, so we keep pretending it binds). When
  it returns: re-add `WOWLIB_BUILD_LUA`, the `if(WOWLIB_BUILD_LUA)` block in
  Dependencies.cmake, and the `welder_luabridge_add_module` target in
  bindings/CMakeLists.txt. LuaBridge3 was pre-declared with `GIT_SUBMODULES ""`
  BEFORE welder's declaration (FetchContent first-declaration-wins): it is
  header-only, and its submodules (googletest, luau, ravi) are hundreds of MB the
  build never uses — keep that when reinstating.
- **One CMake binary per build dir.** The FetchContent populate sub-builds are
  mtime/stamp-based and effectively owned by whichever cmake configured last;
  alternating binaries (CLion's bundled 4.3.1 vs Homebrew 4.3.4) re-cloned
  CascLib/Catch2 on EVERY switch (~40-100s per configure). Resolution: CLion is
  pointed at the system (Homebrew) CMake. Also never run two configures against
  one build dir concurrently — racing populates corrupt checkouts (symptoms
  seen: half-cloned LuaBridge3, casc_static "No SOURCES"); fix with
  `rm -rf build/<cfg>/_deps/<dep>-*` and reconfigure. CLion auto-reload is off.

## Quirks found (2026-07)
- CMake 4.x refuses StormLib/CascLib's ancient `cmake_minimum_required` →
  `set(CMAKE_POLICY_VERSION_MINIMUM 3.5)` before `FetchContent_MakeAvailable`.
- **gcc nested-class NSDMI late-parsing**: a `= {}` default argument cannot
  aggregate-initialize a *nested* struct with default member initializers while
  the enclosing class is still open (hit by `CsvListfile::load(Options = {})`).
  Fix pattern: hoist the options struct to namespace scope
  (`CsvListfileOptions`) + `using Options = ...;` alias inside the class.
- StormLib on POSIX has no `GetLastError` — use `SErrGetLastError()`. CascLib
  does define `GetCascError()` everywhere.
- Both dep .cpps are warning-noisy; warnings (`-Wall -Wextra
  -Wno-missing-field-initializers`) go on wowlib targets only.
- **SFileOpenArchive MUST pass `MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES |
  MPQ_OPEN_NO_HEADER_SEARCH`**: without them StormLib parses each archive's
  internal (listfile)/(attributes) on open — 7s for 3.3.5a common.MPQ vs 10ms
  with the flags (measured). Exact-path reads only need the hash table. A future
  enumeration feature must load listfiles on demand (SFileAddListFile), not at
  open.
- storm/casc_static get `-O2` even in Debug configs (Dependencies.cmake) — never
  debugged into, and CascLib's 2.17M-entry manifest parse is ~2-3x slower at -O0
  (a 9.2.7 storage open is ~2.3s optimized; that part is genuine work).
- Catch2 SECTIONs re-run the whole TEST_CASE (storage re-opened per section) —
  fine now that opens are fast.
## macOS: never `cp` OVER an installed .so — rm first (AMFI SIGKILL)
Copying a rebuilt `wowlib.abi3.so` over the existing file (`cp new old`)
rewrites the same inode; macOS AMFI's code-signature cache still holds the old
signature, and the *next* page-in from that file kills the process with SIGKILL
(exit 137, no Python traceback — and it looks like a crash in whatever ran
last, e.g. `gc.collect()`). lldb-attached processes are immune, which makes it
extra confusing. Rule: `rm dest && cp src dest` (fresh inode) whenever
refreshing the venv extension or stubs.
## Bindings build speed & incrementality (2026-07-30)
The 905-table Release bindings build is heavy: one shard TU (~28 tables x 4 eras
of weld_type reflection/instantiation) is ~1 min at -O2 (measured: 57-table
shard = 123s -O2 / 101s -O1 / 83s -O0; `-fsyntax-only` 45s, so ~60% is
codegen+optimization, ~40% frontend). It parallelizes perfectly across shards.
- **ALWAYS build via the Ninja preset** — `cmake --preset gcc16-bindings-release`
  then `cmake --build --preset gcc16-bindings-release`. A hand-rolled
  `cmake -B build/release ...` defaults to **Unix Makefiles**, and `cmake --build`
  does NOT pass `-j` to make → single-threaded (16 shards x 123s serial ≈ 33 min;
  that was the "half-hour build"). Ninja auto-parallelizes → ~4-6 min.
- **Incrementality is already good — the db shards are ISOLATED from the world
  formats.** A shard's 242 transitive headers pull only
  `formats/common/version_range.hpp`; touching an ADT/WMO/M2/WDT/WDL header does
  NOT rebuild any db shard (verified with `g++ -M`). Editing a db *table* header
  rebuilds only its one shard; editing `db/table.hpp` or a codec header rebuilds
  ALL shards (the shared facade — unavoidable, but rare).
- **PCH does NOT help** (measured: identical time with/without a precompiled
  `db_shard.hpp`) — the cost is per-shard reflection/instantiation of the tables,
  not parsing the shared prologue. Don't add one.
- `WOWLIB_DB_SHARDS` (default 32) trades parallel core-utilization vs incremental
  blast radius vs a little redundant prologue parsing; keep it a small multiple of
  the core count. The release preset uses `-O2` (not -O3) — the bindings are
  registration glue, so -O2 compiles ~18% faster per shard for negligible runtime
  cost. See [[release-build-gotchas]] (memory) for the -Os/NOMINSIZE trap.
- To move a build dir between generators (Makefiles<->Ninja), the FetchContent
  `_deps/*-subbuild` caches are generator-specific: `rm -rf _deps/*-subbuild
  _deps/*-build CMakeCache.txt CMakeFiles` but KEEP `_deps/*-src` (downloaded
  sources) to reconfigure without re-downloading storm/casc/welder/nanobind.
- **`-static-libstdc++` alone is NOT enough for a Linux artifact that shares a
  process with another libstdc++ user** (numpy in every Python/Blender process,
  ICU in every .NET process). The static copy's STB_GNU_UNIQUE symbols (locale
  facet ids etc.) stay exported, the glibc loader merges UNIQUE symbols
  process-wide even under RTLD_LOCAL, and gcc-16.2 code cross-bound to an older
  system libstdc++'s locale data segfaults on the first iostream/format call
  (bindings module, 2026-08-24 — masked for months by the LD_LIBRARY_PATH the
  test steps used to export). The pair that works: `-static-libstdc++
  -static-libgcc -Wl,--exclude-libs,ALL`, plus CI gates (readelf: no NEEDED
  C++ runtime, no ` UNIQUE ` dynsyms). MODULE libraries read
  CMAKE_MODULE_LINKER_FLAGS — SHARED flags never reach a nanobind extension.
