# CI server & self-hosted integration runner

## The box
- Dedicated CI VPS (address in the user's private notes — this file is public),
  Ubuntu 26.04 LTS, 2 cores, 3.7 GiB RAM (+16 GiB swapfile at `/swapfile` —
  gcc-16 needs it), 3.3 TiB disk. SSH as root with key auth.
- Toolchain from apt (Homebrew refuses root): gcc-16 / g++-16, cmake, ninja —
  but apt's gcc-16 trunk snapshot (r16-8246) REJECTS wowlib's reflection code
  (consteval ICE-grade errors in offset framework), so the box never builds
  wowlib: the ci-integration build job compiles the test binary on the hosted
  runner (Homebrew gcc-16, static libstdc++/libgcc) and ships it over as an
  artifact; the box only executes it. python3 stays needed for dbdgen tests.
- Torrent box: transmission-daemon seeds the client downloads from
  `/var/lib/transmission-daemon/downloads`.

## Client installs — /root/WoWClients/<canonical version>
One directory per canonical version name (`2.4.3`, `3.3.5a`, `5.4.8`, `6.2.3`,
`7.3.5`, `8.3.7`, `9.2.7`, `10.2.7`). Directory-shaped torrents are hardlinked
(`cp -al`, same filesystem — near-zero cost, seeding keeps working); archives
were extracted (`/root/unpack_clients.sh`, log `/root/unpack_clients.log`).
- 1.12.1 (**flat** stock layout, no Data/{locale}/ tier), 2.4.3 (WoWCircle,
  enUS+ruRU, **lowercase `data/`**), 3.3.5a (CircleL enUS), 4.3.4 build 15595
  (ruRU), 5.4.8 build 18414 (Data dir only, no exe), 6.2.3.20886 (WoD CASC,
  enGB+ruRU), 7.3.5/8.3.7/9.2.7/10.2.7 = WoWCircle CASC repacks (mostly
  ruRU-tagged). **8.3.7 and 10.2.7 need CascLib > 3.0** (pin bumped to master
  in cmake/Dependencies.cmake — 3.0's open fails on them).
- Missing: TWW 11.x only. All 10 present clients pass the sweep + full suite
  (first full-green run 2026-08-07, 165 cases / 614k assertions on the box).
- `community-listfile.csv` lives in `/root/WoWClients/` too; the workflow
  re-downloads it when older than 30 days.

## Runner
- `/root/actions-runner`, systemd unit
  `actions.runner.skarndev-wowlib.wowlib-clients.service`, runs **as root**
  (needed to read /root/WoWClients; Homebrew was dropped in favour of apt for
  the same reason). Labels: `self-hosted, Linux, X64, wowlib-clients`.
- Re-registration: `gh api -X POST repos/skarndev/wowlib/actions/runners/registration-token`,
  then `RUNNER_ALLOW_RUNASROOT=1 ./config.sh ...`.

## Workflow — integration lives INSIDE .github/workflows/ci-linux.yml
(ci-integration.yml was folded away 2026-08-07 — its build duplicated the
ci-linux release build.)
- The `release` job builds gcc16-release with
  `-static-libstdc++ -static-libgcc` (binary must run against the box's
  runtime; hosted glibc is older than the box's, so glibc is fine), runs
  ctest, patchelfs the Homebrew loader/RUNPATH out, uploads `wowlib_tests`.
- The `integration` job (`needs: release`, runs-on wowlib-clients) downloads
  the artifact, exports `WOWLIB_TEST_DATA_DIR=$GITHUB_WORKSPACE/tests/data`
  (runtime override in tests/unit/unit_env.hpp — the compile-time fixture
  path is wrong for a travelling binary) and runs the Catch2 suite + dbdgen
  python tests directly (no ctest on the box — CTest metadata bakes
  hosted-runner paths).
- **`if: github.event_name != 'pull_request'` on the integration job is the
  fork-PR safety gate** (public repo + root runner) — keep it if the workflow
  is ever restructured. push-to-main has paths-ignore for docs-only changes.

## Test-side environment contract
`tests/integration/integration_env.hpp` resolves installs by directory name
under `WOWLIB_TEST_CLIENTS_DIR` — canonical bare-version names first
(`3.3.5a`), the older descriptive local-Mac names as fallback. Locale and
`Data/` vs `data/` casing are detected per install, not hardcoded (repacks
differ). `test_all_clients_open.cpp` sweeps every canonical directory present:
facade open + probe read (MPQ: `DBFilesClient/Map.dbc`; Legion+: `map.db2` via
listfile; WoD: open-only — its CASC root is name-hash keyed, no FileDataIDs).

## /tmp is a 1.9 GiB tmpfs (learned 2026-08-08 the hard way)
Box jobs set TMPDIR=/root/tmp (disk). Never park binaries or listfile copies
in /tmp — an ENOSPC there truncated an audit listfile copy mid-write and
zeroed 10.2.7's enumeration; the run then died at the end and (pre-fix)
uploaded no artifact. The audit upload step is if: always() now.

## Hosted-CI shape (2026-08-17)
One configuration per platform: Release only — it is what ships and what the
integration/audit artifacts come from; the Debug legs (linux `debug` job,
macos matrix entry) were dropped as pure duplication (~75 min/platform/push).
Every hosted configure passes -DWOWLIB_WERROR=ON (CompilerSettings option
appending -Werror to WOWLIB_CXX_FLAGS — wowlib, wowlib_tests, the db-corpus
object lib and wowlib_py; deps and welder-generated shim targets are not
covered). Local builds stay non-Werror so a new gcc patchlevel's novel
warning never blocks development. Since 2026-08-17 WOWLIB_CXX_FLAGS is
welder's FULL strict set (-Wconversion/-Wsign-conversion/-Wshadow/
-Wold-style-cast/...) so nothing surfaces only through welder's generator
TUs; the one carve-out is wowlib_py compiling -Wno-shadow — gcc-16 reports
a template-for loop variable as shadowing itself, once per expansion
(215 bogus hits across the binding TUs' 16 enumerator loops). Dep noise is
silenced at the source: storm/casc compile -w, their headers and stb_dxt
are SYSTEM includes, and Dependencies.cmake scopes CMAKE_WARN_DEPRECATED
off (CACHE form) + CMP0077 NEW for the dep configures.
ci-linux also has a `csharp` job: gcc16-csharp preset build (native shim +
generated wrapper + facades), then `dotnet test tests/csharp` — the C#
equivalent of the bindings job's pytest gate (~17 min total). Two travelling
gotchas, both fixed 2026-08-17: the test csproj copies the native shim
EXPLICITLY (the Wowlib project's copy-to-output item flowed transitively on
macOS but not Linux), and the job exports the Homebrew-gcc LD_LIBRARY_PATH
before dotnet test (GLIBCXX_3.4.35, same story as the bindings job's pytest
step). Relatedly, test_dbd_loader self-skips when its baked build-tree
WoWDBDefs path is absent (env WOWLIB_TEST_DBDEFS_DIR overrides) — the
travelling binary hits this on the box; hosted release still runs parity.
