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
| welder | commit cf01a75 (no tags yet) | header-only, target `welder::headers` |
| Catch2 | v3.9.1 | `Catch2::Catch2WithMain` + `catch_discover_tests` (extras in module path) |

## Configure-time performance (2026-07-18)
- `FETCHCONTENT_UPDATES_DISCONNECTED ON` in Dependencies.cmake: every pin is an
  immutable tag/commit, and without it each reconfigure ran a network `git fetch`
  per dependency (~3 min wall-clock at 17% CPU). No-op reconfigure now ~1.4s.
- LuaBridge3 is pre-declared with `GIT_SUBMODULES ""` BEFORE welder's declaration
  (FetchContent first-declaration-wins): it is header-only, and its submodules
  (googletest, luau, ravi) are hundreds of MB the build never uses.
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