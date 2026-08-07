# Round-trip audit subsystem

Read when: touching `src/wowlib/audit/`, `tools/audit/`, or the enumeration
APIs (`MpqStorage::enumerate_paths`, `CascStorage::enumerate_fdids`,
`FileSystem::enumerate_paths`).

## Shape (decided 2026-08-07; replaced an earlier standalone-CLI plan mid-build)

- **The audit is a welded library surface + a pytest driver, NOT a C++ tool.**
  The CLI/report/JSON layers were cut; Python does orchestration, CSV/JSON
  reporting and client discovery, C++ does exactly what only C++ can:
  enumerate + round-trip one file per call.
- `wowlib::audit` (src/wowlib/audit/):
  - `roundtrip.hpp` — the welded surface: `RoundtripReport` {ok, stage, error,
    unknown_chunks (fourcc per occurrence)} and `Auditor::roundtrip(fs, path,
    version)` (static). NO semantic validation — read→write→compare only.
  - `detail.hpp` — unwelded internals: outcome ctors, `first_divergence[_chunked]`,
    `with_version` (runtime→NTTP bridge over the 11 versions:: constants),
    `version_supported(grid, V)` guard, `guarded` (exception→failed outcome),
    `FormatDrivers` class (one static per format).
  - One driver TU per format (`roundtrip_{wmo,m2,adt,wdt_wdl,blp}.cpp`) so the
    version matrices compile in parallel. Drivers are ports of the
    tests/integration round-trip helpers with outcomes instead of REQUIRE, and
    the reflection diff replaced by WRITE-STABILITY (write→parse→write-again,
    byte-compare) for M2/ADT; WMO/WDT/WDL/BLP stay byte-perfect vs original.
- **Classification is by extension inside Auditor::roundtrip** (canonicalized
  first): wmo groups (`_\d{3}.wmo`) / `_lod` wmos / aux wdts (8 satellite
  suffixes: occ lgt fogs mpv tex wmo psd pd4) / adt split files
  (_tex0/_tex1/_obj0/_obj1/_lod) / .mdx/.mdl / unknown extensions all return
  ok=true with a `skipped:<reason>` stage. Unsupported format×version combos
  → `skipped:unsupported-version`; a version not among the 11 targeted
  releases → failed{stage="dispatch"}.
- **ADT alpha format is re-derived per call** by reading the map's WDT next to
  the tile (parent dir names the map); nothing is cached. WDT missing/broken →
  failed{stage="wdt read"/"wdt parse"} — that is data.
- **Gotcha:** driver TUs open `using namespace wowlib(::formats)(::audit)`,
  which makes bare `detail::` ambiguous (audit::detail vs formats::detail vs
  wowlib::detail) — qualify as `audit::detail::`.

## Enumeration APIs

- `MpqStorage::enumerate_paths()` — archives open NO_LISTFILE, so it calls
  `SFileAddListFile(handle, nullptr)` per archive (loads the internal listfile
  on demand, walks the patch chain) before SFileFindFirstFile/Next. Skips
  StormLib metadata names and the nameless "File########.ext" placeholders
  (reads by those would miss). Archives that cannot enumerate are skipped
  silently; std::set gives dedup+sort in one go.
- `CascStorage::enumerate_fdids()` — CascFindFirstFile("*"), keeps
  bFileAvailable + valid dwFileDataId. WoD-era roots are name-hash keyed →
  empty result (expected).
- `FileSystem::enumerate_paths()` (welded) — MPQ passthrough; CASC maps fdids
  through the composition's own CsvListfile (unnamed ids dropped; no listfile
  → empty listing).

## The pytest driver (tools/audit/)

- Not part of tests/python; run explicitly:
  `PYTHONPATH=build/bindings/bindings/python:<site> .venv/bin/python -S -m
  pytest tools/audit --clients-dir ... --listfile ... [--max-per-format N]
  [--audit-clients a,b] [--audit-formats wmo,blp] [--report-dir dir]`
  (`-S` + explicit path: the editable-install .pth otherwise shadows the fresh
  build — same trap as the db proof harness).
- Parametrized clients (the 13-dir canonical table from
  test_all_clients_open.cpp) × formats (wmo m2 adt wdt wdl blp). One
  session-cached FileSystem + classified enumeration per client. Round-trip
  failures are report DATA; a test only fails when open/enumerate breaks.
- Output: `<report-dir>/<client>_<fmt>.csv` (path, ok, stage, error) +
  crash-safe `summary.json` + a terminal-summary table.
- CASC clients get a disposable listfile copy per client (open() appends
  registrations to its working CSV).

## Bindings integration

- Umbrella `wowlib.hpp`: `namespace audit` opens LAST (its signatures name
  fs::FileSystem/ClientVersion) + includes audit/roundtrip.hpp.
- bindings/CMakeLists stub OUTPUT gained `stubs/wowlib/audit.pyi`.
- `RoundtripReport::unknown_chunks` (vector<string>) rides the opaque-container
  generator like every other vector member.
