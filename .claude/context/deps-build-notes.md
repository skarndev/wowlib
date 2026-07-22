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

## Pins (FetchContent, cmake/Dependencies.cmake)
| Dep | Tag | Notes |
|---|---|---|
| StormLib | v9.40 | target `storm`; `STORM_SKIP_INSTALL`, `BUILD_SHARED_LIBS=OFF`; links SDK zlib/bzip2 |
| CascLib | 3.0 | target `casc_static`; `CASC_BUILD_STATIC_LIB=ON`, `CASC_BUILD_SHARED_LIB=OFF`, unicode off |
| welder | commit 31b0801 (no tags yet; opaque-container generator + `transform_opaque_container` naming hook + `mark::no_reassign`) | header-only, target `welder::headers`; **must be a pushed commit** (see below) |
| Catch2 | v3.9.1 | `Catch2::Catch2WithMain` + `catch_discover_tests` (extras in module path) |

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