# MPQ archive load order & chain tables

Read when: touching `fs/mpq/mpq_chain.*` or adding support for another MPQ-era
client version.

## The model
The client hardcodes its base archive list in the binary and wildcard-loads
patches after it; a later-loaded archive overrides earlier ones file-by-file.
`MpqStorage` opens every chain member standalone and searches in REVERSE chain
order (last loaded wins).

## Implementation (2026-07-19) — reflects the Ghidra findings below
`expand_chain` (pure, StormLib-free, unit-tested with fake trees) produces the
chain in load order:
- **Base tier**: fixed `ChainEntry` rows (`Fixed`/`LocaleFixed`) in table order,
  lowest priority. Internal precedence among base archives is the established
  community order (`common, common-2, expansion, lichking`, then the locale
  base archives) — the binary's exact base ordering is still unresolved (see
  round-2 note) but low-impact since patches override all base.
- **Patch tier** (`PatchScheme::ClassicWildcard`): globs `Data/` for
  `patch.MPQ` + single-char `patch-?.MPQ`, and `Data/{locale}/` for the
  `patch-{locale}[-?]` equivalents. Base and locale matches go into **ONE list**
  sorted by the client's key: extension-stripped bare filename, comparator
  **case-insensitive** (`ci_less` ↔ `__strnicmp`). Base and locale patches
  **interleave** — because the comparator is case-insensitive, an uppercase base
  `patch-Z` (→ `z`, 0x7A) sorts *after* the locale infix (`enus`, first char `e`
  0x65), so custom base letter-patches above the locale code outrank the locale
  patches (`patch-Z` overrides `patch-enUS`). This matches `FUN_00405ab0`'s sorted
  pass, which globs both wildcards into one list. (Superseded the earlier
  "separate groups, locale strictly last" model — that rested on round-1's
  case-*sensitive* reasoning, invalidated by the round-2 `__strnicmp` finding.)
- **Directory members**: any base or patch slot may be a folder of loose files
  (`ChainMember::is_directory`); a real archive file of the same name wins.
  `MpqStorage` indexes each loose dir by canonical in-game path at open, so reads
  are case-insensitive. Missing archives are skipped silently.

## Open follow-up — secondary patch pass (not yet modeled)

`FUN_00405ab0` runs TWO passes over its 12-entry template table. Pass 0 (the only
one that gets sorted) globs the single-char wildcards `patch-?.MPQ` +
`patch-{loc}-?.MPQ` in `Data/` — this is what `expand_chain` models. Pass 1 is
UNsorted and appended after: the fixed `patch.MPQ` / `patch-2/3/4` (+ locale
forms) re-adds, plus a second `..\Data\` (parent-of-cwd) root for all of them.

We do NOT model pass 1. Impact is limited and does not touch the verified
letter-patch interleaving (letters come only from the sorted pass 0):
- The bare `patch.MPQ` and `patch-2/3/4` are also matched by the pass-0 wildcard
  (except bare `patch.MPQ`, no `-` suffix), so they already appear; the exact
  final priority of these low/bare patches vs. their re-add depends on the
  archive-open dedup, which we didn't decode.
- The `..\Data\` root only matters when the client runs from a subdirectory —
  irrelevant to a normal `Data/` layout.

To get bit-exact parity (only if a real repack ever depends on it): run a client
under a debugger, dump the final per-archive priority list, and reconcile the
pass-1 dedup/append against `expand_chain`. Until then, treat pass 1 as a known,
low-impact gap. GhidraMCP here has no memory-read tool, so this needs a dynamic
check, not more static tracing.

## ✅ Ghidra verification round 2 (2026-07-19, live GhidraMCP)

Re-derived against `Wow.exe` 3.3.5a with the loader confirmed as **`FUN_00405dd0`**
(the real top-level, called from `FUN_004067f0`; embedded build path
`…wow-patch-3_3_5_a-bnet\…\PatchFiles.h` confirms it's genuine 3.3.5a patch code).
Sequence inside it: `FUN_00402b90()` mounts base → `FUN_00405ab0()` assembles the
patch list → archives opened with explicit priority numbers.

CONFIRMED (was "still open"):
- **Priority-wins direction:** base archives open at `0x3f` **decrementing**;
  patches open at `0x40` **incrementing**; higher priority wins ⇒ **every patch
  overrides every base archive.** (Resolves the `FUN_00421950` question.)
- **Patch order:** list sorted by comparator `FUN_00401200` then opened in reverse.
  Comparator is `-__strnicmp(a,b)` — **case-INSENSITIVE** (correction: the round-1
  note below said `-strcmp`; it is `strnicmp` via `FUN_0076e780`). Descending sort +
  reverse open ⇒ **ascending bare-filename → higher priority.**
- **Sort key** (`FUN_00405a10`) = `sprintf("%s%s", <found-name>, ctx+8)` = the **bare
  filename**; directory root is NOT part of the key. Confirms the round-1 reasoning.

Base-archive precedence — RESOLVED by Sergey (domain fact), consistent with the
verified `0x3f`-decrementing base priority (winner processed first / higher on the
`0x3f` side, higher-wins):
- **`common-2` overrides `common`** (common-2 is the newer supplemental half).
- **`lichking` (WotLK) overrides `expansion` (TBC)** — newer expansion wins.
So base order (highest precedence first) puts `common-2` before `common` and
`lichking` before `expansion`. NB: this reverses round-1's asserted table order
(`expansion, lichking, common, common-2`) for these pairs — round-1 had the
direction backwards.

Still un-byte-verified but IRRELEVANT to resolution: cross-pair ordering of
**non-overlapping** base archives (e.g. `common*` vs `sound`/`texture`/`model`…) —
they never share paths, so their relative priority can't change any lookup. (Name
pointers are split across `.data` tables `0x00ab6160` stride 0x10 and `0x00ab7b14`;
`common-2`/`lichking` have no pointer xref — contiguous-string arithmetic. GhidraMCP
here has no memory-read tool to dump them, and we no longer need it.)

## ⚠️ Ghidra verification round 1 (2026-07-19) — the "Verified" order below is WRONG

Decompiled the real archive loader in `Wow.exe` 3.3.5a (build 12340) with
Ghidra headless. The mechanism is NOT "fixed list, custom patches appended in a
known sequence". Key functions:
- `FUN_00405dd0` — top-level loader. Opens archives with an explicit, *incrementing*
  priority number starting at `0x40`; iterates the hardcoded base table at
  `.data:0x00ab6160` (stride 0x10). Base table order in the binary is
  `expansion, lichking, common, common-2, {loc}/locale, {loc}/speech,
  {loc}/expansion-locale, {loc}/lichking-locale, {loc}/expansion-speech,
  {loc}/lichking-speech, development, streaming, streamingloc` (plus the split
  `interface/model/texture/...` archives that don't ship in 3.3.5a retail).
  Our `wotlk_entries` order `common, common-2, expansion, lichking` is NOT the
  binary's order.
- `FUN_00405ab0` — patch-chain builder. Enumerates patches by a **single-char
  wildcard** `patch-?.MPQ` (base, dir `Data\`) and `patch-%s-?.MPQ` (locale, dir
  `Data\%s\`), plus fixed `patch.MPQ / patch-2 / patch-3 / patch-4` and locale
  equivalents, and a second `..\Data\` root. All go into ONE list. `?` is a
  single char, so `patch-10.MPQ` would never match (our `4..9` then `A..Z` loop
  yields the same SET for single-char names, but by a different mechanism).
- The list is then **sorted** (`FUN_0047b800`, a quicksort) with comparator
  `FUN_00401200 = -strcmp(a,b)` → reverse/descending string order. Sort key is
  built in `FUN_00405a10` as `sprintf("%s%s", <found-name>, ctx+8)`. Archives are
  then opened in list order with increasing priority. So final precedence is
  **alphabetical**, not insertion-order. Our impl (fixed append + search in
  reverse) does not model this.

RESOLVED — base-vs-locale precedence:
- The directory iterator (`FUN_00427660`) gets the search root (`Data\`,
  `Data\<loc>\`) SEPARATELY from the name pattern, so the find-data name — hence
  the sort key in `FUN_00405a10` — is the **bare filename** (`patch-enUS-2.MPQ`),
  not path-qualified. Directory is not part of the sort key.
- Open priority `0x40++` with higher-wins; base archives take priorities <0x40
  (`local_c` counting down from 0x3f), so every patch outranks every base archive.
- Ascending bare-filename order ⇒ base suffixes `2`-`9`,`A`-`Z` (≤0x5A) sort
  BEFORE locale patches (lowercase infix `d/e/f/k/r/z…` ≥0x64) ⇒ locale patches
  get higher priority. **Locale patches load after and override base `Data\`
  patches.** (Matches long-standing community behavior.)

RESOLVED — bare-`patch.MPQ` tie-break (confirmed by Sergey, known fact):
`patch.MPQ` sorts BEFORE `patch-2.MPQ`, i.e. the compare is effectively
**extension-agnostic** — it ranks `patch` < `patch-2` (shorter/prefix first),
not `patch.` > `patch-`. So within each group the extensionless name is lowest
priority. Final within-group order: `patch` < `patch-2` < … < `patch-9` <
`patch-A` < … < `patch-Z`, and likewise for the locale group.

## Directory-backed archives ("MPQ as a folder")  [REQUIREMENT — Sergey]
The client also loads an archive slot when a **directory** of that name exists
instead of a real MPQ file (e.g. `Data/patch-4.MPQ/` as a folder of loose files
keyed by their in-game path, `Data/patch-4.MPQ/World/Maps/...`). It participates
in the SAME chain/sort/priority as real archives (a folder named `patch-4.MPQ`
sorts identically to the archive). Implications for us:
- `expand_chain` currently gates on `std::filesystem::is_regular_file`
  (`push_if_exists`) → it SKIPS directories. Must accept dirs too, and the new
  sort must key on the extension-agnostic bare name regardless of file-vs-dir.
- `MpqStorage::OpenedArchive` assumes every member is a StormLib `HANDLE`. A
  chain member must become archive-OR-directory (variant): StormLib for files, a
  loose-directory source for folders. `read_file`/`exists` dispatch per member;
  the reverse-order (last-wins) traversal is unchanged.
- Loose-dir resolution maps `key.path` (`\`-separated in-game path) to
  `<dir>/<path>` on disk. Resolved semantics (Sergey):
  - **Real archive file wins** over a same-name directory: if both
    `patch-4.MPQ` (file) and `patch-4.MPQ/` (dir) exist, take the archive and
    ignore the directory for that slot. (Enumeration: emit ONE slot per name;
    prefer the regular file.)
  - **Case-insensitive** path matching: build a lowercased filename index per
    loose dir at open time so lookups match the client on case-sensitive
    filesystems too. Recursion (nested subdirs mirroring the in-game path) is
    assumed; ANY chain slot (base archives included) may be a directory.

## Verified 3.3.5a (build 12340) order  [SUPERSEDED — see warning above]
1. `common.MPQ`, `common-2.MPQ`, `expansion.MPQ`, `lichking.MPQ`
2. `{loc}/locale-{loc}.MPQ`, `speech`, `expansion-locale`, `expansion-speech`,
   `lichking-locale`, `lichking-speech`
3. `patch.MPQ`, `patch-2.MPQ`, `patch-3.MPQ`
4. `{loc}/patch-{loc}.MPQ`, `-2`, `-3`
5. custom `patch-{4..9}.MPQ` then `patch-{A..Z}.MPQ` (community convention;
   numbers before letters — ASCII '9' < 'A')
6. `{loc}/patch-{loc}-{4..9,A..Z}.MPQ`

`{loc}` is supplied by the caller (`FileSystemSettings.locale`, default enUS); we
no longer scan/auto-detect it. `MpqStorage::open` only asserts the `Data/{code}/`
directory exists, failing with `StorageOpenFailed` otherwise. Distractors that
would have confused a scan: `base-{loc}`, `backup-{loc}` archives.

Note: pywowlib sorted ALL `Data/*.MPQ` alphabetically — approximately right for
3.3.5a, wrong in general; don't copy that.

## Adding other versions
- **1.12.x: DONE (2026-07-28).** `vanilla_base` table in `mpq_chain.cpp` +
  `MpqChainSpec{versions::vanilla, …, ClassicWildcard}`. Base tier = the media/data
  archives (`base, dbc, fonts, interface, misc, model, sound, speech, terrain,
  texture, wmo`) + the two `Data/{loc}/` locale rows (`locale-{loc}`, `speech-{loc}`);
  patch tier reuses ClassicWildcard. Base order **VERIFIED** against
  `samwhosung/benilla` (a from-scratch 1.12.1 Rust client;
  `crates/benilla-formats/src/lib.rs` `VANILLA_LOAD_ORDER`) — identical base list
  and order (`base, dbc, fonts, interface, misc, model, sound, speech, terrain,
  texture, wmo`), then `patch.MPQ`, `patch-2.MPQ` on top. benilla stops at
  `patch-2` and carries no locale rows (it targets enUS base content); our
  ClassicWildcard glob reproduces its patch order and extends to `patch-3`/letter
  patches, and we keep `locale-{loc}`/`speech-{loc}` for real localized installs
  (absent members skipped). Order among base archives is anyway immaterial — they
  partition the namespace (never share a path) and every patch overrides all base.
  Integration-covered by
  `test_mpq_112_client.cpp` + the `1.12.2` cases in the ADT/WMO/M2/WDT-WDL
  round-trip files, opened with **`Locale::ruRU`** (the local 1.12.2 install is a
  ruRU repack: no enUS dir; it also folds localization into `base.MPQ` +
  `Data/ruRU/patch-N.MPQ`, so the `locale-{loc}`/`speech-{loc}` rows and the
  `patch-{loc}` locale glob simply find nothing there — world data still serves
  from terrain/model/wmo/patch).
- **2.4.3: DONE (2026-07-29).** `tbc_base` table in `mpq_chain.cpp` +
  `MpqChainSpec{versions::tbc, …, ClassicWildcard}`. Structurally the WotLK chain
  minus the WotLK-only base archives: base = `common` + `expansion` (no `common-2`,
  no `lichking`) + the four `Data/{loc}/` locale rows (`locale-`, `speech-`,
  `expansion-locale-`, `expansion-speech-`); patch tier reuses ClassicWildcard.
  `expansion` outranks `common` (later in table, reverse search). The loader
  mechanism is the SAME Ghidra-verified 1.x/2.x/3.x family; base NAMES are canonical
  and on disk, so no fresh Ghidra run. As on 3.3.5a, `base-{loc}.MPQ`/`backup-{loc}.MPQ`
  are on-disk distractors NOT in the binary table → no row. The local 2.4.3 install
  ships full **enGB** and **ruRU** locale sets; integration opens the enGB chain
  (`test_mpq_243_client.cpp` + the `2.4.3` cases in the ADT/WMO/M2/WDT-WDL round-trip
  files). This 2.4.3 install prompted a real WDL fix — see [[wowdev-chunk-version-corrections]]:
  its **WDL**s carry per-tile `MAHO` hole masks, which led to correcting wowlib's
  MAHO gate from WotLK to TBC (a client scan proved MAHO debuts in TBC, not WotLK
  as wowdev.wiki implies). Note: `MAHO` is a WDL chunk — the WDT/WDL round-trip
  test formerly shared ONE unknown-chunk histogram across both files, which made a
  WDL chunk look WDT-sourced; that histogram is now split per file kind.
- **4.3.4 / 5.4.8: DONE (2026-07-30).** `PatchScheme::UpdateChain` implemented:
  `expand_chain` globs `wow-update[-base]-{build}.MPQ` (Data/, attach prefix
  "base") and `wow-update-{locale}-{build}.MPQ` (Data/{locale}/, prefix the
  locale code), sorted ascending by build, emitted as `ChainMember.incremental`
  entries carrying their prefix. `MpqStorage::open_chain` attaches each to
  every already-open archive of the SAME Data directory via
  `SFileOpenPatchArchive`; StormLib then serves PTCH-patched content through
  the base handles transparently. GOTCHA: `SFileHasFile` checks only the base
  hash table and cannot see files ADDED by updates — patched archives resolve
  read_file/exists through `SFileOpenFileEx` directly (continue on
  ERROR_FILE_NOT_FOUND). `cata_base`/`mop_base` tables added; 4.3.4 verified
  end-to-end by the full DBC/DB2 corpus round-trip (test_dbc_corpus_434.cpp,
  ruRU install symlinked into ~/WoWModding/Clients as "WoW Cata 4.3.4"); the
  MoP table is a safe superset, UNVERIFIED until the local 5.4.8 download
  completes.
## Vanilla (1.x) locale tier — optional (2026-08-06)
Stock 1.12 installs are FLAT: `Data/` holds base/dbc/fonts/.../patch-2 with NO
`Data/{locale}/` subdirectory at all (the locale tier entered the layout with
TBC). Only some later repacks (e.g. the local "WoW Classic 1.12.2" ruRU one)
retrofit a locale dir. `MpqStorage::open` therefore requires `Data/{code}/`
only for major >= 2; for 1.x the `LocaleFixed` entries and locale patches
mount only when the directory exists. The CI box's `1.12.1` is the flat kind.

## Enumeration misses base archives on wow-update chains (2026-08-10, OPEN BUG)

`FileSystem::enumerate_paths()` returns almost nothing for the two clients whose
data sits behind incremental `wow-update-*.MPQ` chains, while direct reads of the
same archives work perfectly.

REPRODUCED LOCALLY on a complete 4.3.4 install (8 base archives + 3 wow-update,
~12.5 GB): **125 paths** — 28 blp, 22 dbc, 15 lst, 15 skin, 10 ogg, 8 lua, 7 m2,
5 db2. The blp (28) and m2 (7) counts match the CI box's audit EXACTLY, so it is
the same defect, not a broken install. Everything enumerated is late-patch
content; nothing from base-Win/art/world/world2/expansion1-3 appears.

Reads are unaffected: 700 Cata WMO groups were read by path from that same
install during the MDAL investigation, and the 4.3.4 DBC corpus round-trips
byte-perfectly in CI.

Impact on the nightly audit: 4.3.4 contributes 35 files and 5.4.8 contributes
18,362 (below vanilla-era 3.3.5a's 142,737, which cannot be right for MoP), so
those two clients have effectively NO round-trip coverage while appearing green
in the report. The 3.05M-file / 99.95% headline is measured over a corpus missing
~300k files.

Not yet diagnosed: whether the chain's base archives are skipped when building
the path index, or their `(listfile)` is not read. Note enumerating that install
takes ~50 min from a USB HDD, so iterate against a local-disk copy.
