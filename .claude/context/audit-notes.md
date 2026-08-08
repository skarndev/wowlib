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
  the tile (parent dir names the map); nothing is cached. An unreadable map
  WDT → skipped:no-map-wdt (addressing limitation — unnamed "unkmaps" tiles);
  a WDT that reads but will not parse → failed{stage="wdt parse"} (data).
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

## CI shape (ci-audit.yml)

- ci-audit builds NOTHING (since 2026-08-08): the audit job on the box locates
  the newest successful main ci-linux run holding the
  `wowlib-python-linux-x86_64` artifact (github-script) and cross-run
  downloads it (download-artifact v4 run-id + github-token). The ci-linux
  bindings job produces that module box-portable: static libstdc++/libgcc +
  patchelf --remove-rpath. The former duplicate hosted build cost ~75 min per
  nightly; the box itself cannot compile wowlib (apt gcc-16 rejects
  reflection).
- Consequence: an audit dispatch right after a source push runs against the
  PREVIOUS build until that push's ci-linux bindings job finishes — wait for
  ci-linux before dispatching when the fixes under test just landed.

## Bindings integration

- Umbrella `wowlib.hpp`: `namespace audit` opens LAST (its signatures name
  fs::FileSystem/ClientVersion) + includes audit/roundtrip.hpp.
- bindings/CMakeLists stub OUTPUT gained `stubs/wowlib/audit.pyi`.
- `RoundtripReport::unknown_chunks` (vector<string>) rides the opaque-container
  generator like every other vector member.

## First fleet findings (capped 50/format run, 2026-08-07 — run 31180230256)
- All 10 clients enumerate and audit — including 6.2.3 WoD (the "name-hash
  root → empty" expectation was wrong: CascFindFirstFile does yield fdids).
- **1.12.1 ADT 31/50 failed**: "MCNK sub-chunk MCAL overruns the chunk" on
  AhnQiraj (+ other early-alphabet) tiles — real vanilla alpha-layer gap, the
  curated tests never sampled these maps.
- **2.4.3 development map**: development.wdt/.wdl fail "required chunk MVER is
  absent" — alpha-era leftovers shipped in the TBC client.
- **Unmodeled chunks**: MOSB in one WMO each of 8.3.7/9.2.7/10.2.7 (legacy
  skybox chunk on modern-version entities?); MLDF x3 in 8.3.7 WDLs
  (undocumented ML-family chunk). All round-tripped verbatim.
- Modern-client WMO skips (36-41/50) are _lodN variants; 9.2.7+ ADT skips are
  no-map-wdt (unnamed "unkmaps" tiles).

## Fleet findings round 2 (2026-08-08, post-tmpfs-fix make-up run)
- The tmpfs truncation had silently DEGRADED late-client enumerations in the
  first uncapped run (9.2.7 m2 59k -> true 91.7k) — treat that run's late
  cells as undercounts; the make-up run 31220400689 has the true 9.2.7/10.2.7.
- True remaining failure surface (before the encrypted/empty reclass): almost
  everything Legion+ is "unknown TACT key" (=> skipped:encrypted); genuinely
  broken: 2 corrupt 9.2.7 repack files (garbage fourccs 'acol'/' iU<'),
  1 9.2.7 adt, 4 cata sentinel m2s, 2.4.3+7.3.5 development-map wdt/wdl
  (MVER absent).
- **MOTV appears in 983 10.2.7 ROOT files** (example
  world/wmo/azeroth/buildings/stormwind/8sw_portalroom01.wmo) — texcoords are
  historically a group chunk; post-wmo_split_groups (9.2+) roots carrying
  MOTV is an unmodeled era shift. Also still unmodeled: CDFP (8.3 item m2s),
  MLDF/MLMB (BfA wdl).
- Parser fixes landed from round 1: MCAL/MCLQ header-authoritative-when-empty
  (AQ -2048 garbage), MCSE 52-byte 1.x payloads preserved verbatim via
  SoundEmitterCodec raw fallback (layout awaits RE), zero-byte + encrypted
  reads reclassified as skips in the audit drivers. Post-fix 1.12.2 slice:
  adt 2478/2478 ok, wmo 815/816.
## Round 3 (2026-08-08): every unknown chunk resolved — all were OUR bugs

- **The audit's WMO rows aggregate root + groups**: a group-file finding is
  attributed to the ROOT's report row/example path. Cost an investigation
  detour once ("MOTV in 983 roots" was really a 4th MOTV in their groups).
- MLDF/MLMB (8.3.7 wdl): were modeled as SL blobs, but the WDL pivot grid had
  no BfA entry so 8.3.7 parsed with the Legion layout. Now a WdlBfA slot +
  BfA pivot; MLDF typed (LodDoodadFade, 1 per MLDD; survey: 33 files); MLMB
  opaque (1 byte per MLMD, WMO-city maps only). WDLLegionToBfa alias split
  into WDLLegion + WDLBfa.
- "CDFP" (8.3.7 m2): really PFDC displayed reversed (M2 magics are forward);
  the gate sat at wowdev's 9.0.1.33978 while 8.3.7 b35662 shipped mid-alpha
  with the backport. New BfA_VisionsOfNzoth_35662 milestone; entities can
  declare `unknown_fourcc_endian` (M2 shell/skel/bone = forward) which the
  scanner diagnostics and audit tally honor.
- MOTV (10.2.7 "roots"): Dragonflight GROUPS ship a 4th texcoord set;
  repeats(3) capacity overflowed into the unknown route. Now repeats(4).
- Verification pattern that worked: extract fleet specimens on the box via a
  scp'd module + /root/audit-venv script, bring files local, byte-perfect
  round-trip through the exact per-range Python class.

- **1.12.2 undercity_144.wmo is shipped-corrupt** (the lone 1.x wmo failure):
  the file is truncated by exactly 1 byte. MOCV declares 1160 (= 290 vertex
  colors x 4, matching MOVT's 290 vertices — the size is right) but only 1159
  bytes exist; the outer MOGP size overruns by the same 1. 3.3.5a ships a
  rebuilt, fully consistent copy of that group. NOT a parser gap — stays a
  report failure per the failures-are-data policy; do not add leniency for it.
