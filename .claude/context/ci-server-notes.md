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
- 2.4.3 (WoWCircle, enUS+ruRU, **lowercase `data/`**), 3.3.5a (CircleL enUS),
  5.4.8 build 18414 (Data dir only, no exe), 6.2.3.20886 (WoD CASC, enGB+ruRU),
  legion/8.3.7/9.2.7/10.2.7 = WoWCircle CASC repacks (mostly ruRU-tagged).
- Missing: 1.12.x (the first download was an installer, not an install — user
  re-downloads), 4.3.4 (never downloaded; its corpus tests skip on the box),
  TWW 11.x.
- `community-listfile.csv` lives in `/root/WoWClients/` too; the workflow
  re-downloads it when older than 30 days.

## Runner
- `/root/actions-runner`, systemd unit
  `actions.runner.skarndev-wowlib.wowlib-clients.service`, runs **as root**
  (needed to read /root/WoWClients; Homebrew was dropped in favour of apt for
  the same reason). Labels: `self-hosted, Linux, X64, wowlib-clients`.
- Re-registration: `gh api -X POST repos/skarndev/wowlib/actions/runners/registration-token`,
  then `RUNNER_ALLOW_RUNASROOT=1 ./config.sh ...`.

## Workflow — .github/workflows/ci-integration.yml
- push-to-main + workflow_dispatch only. **No pull_request trigger**: public
  repo + root runner means fork PRs must never reach this box.
- Two jobs: `build` on ubuntu-latest (Homebrew gcc-16,
  `-static-libstdc++ -static-libgcc` so the binary runs against the box's
  runtime; hosted glibc is older than the box's, so glibc is fine) uploads
  `wowlib_tests`; `test` on the box downloads it, exports
  `WOWLIB_TEST_DATA_DIR=$GITHUB_WORKSPACE/tests/data` (runtime override added
  in tests/unit/unit_env.hpp — the compile-time fixture path is wrong for a
  travelling binary) and runs the Catch2 suite + dbdgen python tests directly
  (no ctest on the box — CTest metadata bakes hosted-runner paths).

## Test-side environment contract
`tests/integration/integration_env.hpp` resolves installs by directory name
under `WOWLIB_TEST_CLIENTS_DIR` — canonical bare-version names first
(`3.3.5a`), the older descriptive local-Mac names as fallback. Locale and
`Data/` vs `data/` casing are detected per install, not hardcoded (repacks
differ). `test_all_clients_open.cpp` sweeps every canonical directory present:
facade open + probe read (MPQ: `DBFilesClient/Map.dbc`; Legion+: `map.db2` via
listfile; WoD: open-only — its CASC root is name-hash keyed, no FileDataIDs).
