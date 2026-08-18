# Classic clients & the flavor axis (2026-08-18)

## The problem

`ClientVersion` orders lexicographically on (major, minor, patch, build), and
every format decision used to read that order as a proxy for engine generation:
`since()/until()` annotations, the canonicalization pivots, `storage_kind()`
(`major < 6 → MPQ`), `expansion_of()`.

That holds for retail. It is false for the Classic products, which are the
**modern client rebuilt from whatever retail branch was current**, shipping
content that looks like an old expansion. Under the old model, WoW Classic Era
1.15.9 reported MPQ storage, canonicalized onto `versions::vanilla`, and mapped
to `Expansion::Vanilla` — while actually being a Midnight-era CASC client with
chunked MD21 M2s, `MAID` WDTs, split ADTs and WDC DB2s.

## The evidence (wago.tools `/api/builds`, pulled 2026-08-18)

| product | line | builds | retail engine spanned |
|---|---|---|---|
| `wow_classic` | 1.13 | 28211–38631 | 8.x → 9.x |
| `wow_classic` | 2.5 (BCC) | 38644–44833 | 9.x |
| `wow_classic` | 3.4 (WotLKC) | 45327–63697 | **9.x → 10.x → 11.x** |
| `wow_classic` | 4.4 (CataC) | 54481–60895 | 10.x → 11.x |
| `wow_classic` | 5.5 (MoPC) | 61735–69155 | 11.x → 12.x |
| `wow_classic_era` | 1.13–1.15 | 38704–69109 | 9.x → 12.x |
| `wow_anniversary` | 2.5 | 65340–69110 | 12.x |
| `wow_classic_titan` | **3.80** | 64393–69137 | 11.x → 12.x |

Two conclusions:

- The version tuple carries **no** engine information. One line (3.4.x) spans
  three retail generations; even one version *number* can (Cata Classic 4.4.0
  shipped at build 54481 on Dragonflight and 57244 on The War Within);
  `wow_classic_titan` calls itself 3.80.
- The **build number does**. Blizzard's counter is global across every product,
  and retail majors occupy clean non-overlapping windows in the CASC era: 8.x
  26926, 9.x 35917, 10.x 46181, 11.x 55666, 12.x 65390.

Build ordering is NOT valid before CASC — concurrent branches overlap there
(MoP beta 15464 predates Cata 4.3.4's 15595; WoD alpha 18125 predates MoP
5.4.8's 18414). That is why the fix is a lineage mapping and not "compare by
build everywhere". No Classic client exists below build 28211, so the two
regimes never meet.

## The model

`core/client_version.hpp`:

- `ClientFlavor { Retail, Classic, ClassicEra, Anniversary }` — a new
  `ClientVersion` member, defaulted to `Retail` so every existing call site is
  unchanged. Stays a scalar so `ClientVersion` remains structural (it is an
  NTTP everywhere).
- `ClientVersion::format_lineage()` — identity for Retail; for Classic, the
  retail release whose engine produced that build, from
  `detail::engine_timeline` (first live/PTR build of each retail major →ceding
  the `versions::` constant modelling it). Builds above the newest modelled
  generation clamp to it, so today's 12.x-era Classic builds resolve to TWW.
  **Adding a Midnight row there and to the format grids fixes retail and
  Classic in one move.**
- `storage_kind()` — `!is_classic() && major < 6 ? Mpq : Casc`.
- `default_casc_product()` — the flavor's TACT code.
- `operator<<` — `"1.15.9.69109 (ClassicEra)"`; welder binds it as
  `__str__`/`__tostring`.

`formats/common/version_range.hpp`: `version_floor()` maps its argument through
`format_lineage()` first. That single line is the whole format-layer fix —
`canonical_version()`, `range_suffix()` and `ranges_valid()` all funnel through
it. `version_slot.hpp`'s `slot<>` does the same, for code naming a `detail::`
template directly.

**The payoff: a Classic version canonicalizes onto an EXISTING retail grid
entry, so Classic support adds no template instantiation, no welded class and
no binary size.** `WMO<classic_cata>` *is* `WMO<tww>`.

## Content axis vs format axis

`expansion_of()` stays the **content** axis — Cata Classic reports `Cata`,
because that is the game it is. For formats, ask
`to_expansion(v.format_lineage())`. `to_expansion()` never matches a Classic
constant (flavor differs), which is correct: a Classic client is not an
expansion release. Both are documented in place; do not "fix" them.

## Surface

- `versions::classic_era / classic_bcc / classic_wotlk / classic_cata /
  classic_mop / anniversary` — newest build of each line as of 2026-08-18.
  These are snapshots of moving targets; bumping one can legitimately move its
  lineage to a newer engine.
- `fs::ClientInstall::detect(path)` — reads `.flavor.info` (product code)
  beside `Data/` and `.build.info` (version table) there or one level up, where
  a multi-flavor install keeps it. CASC-only; MPQ clients and bare repacks
  raise `NotSupported`.
- `fs::FileSystemSettings::detect(...)` — same, wrapped as settings.
- `FileSystemSettings::casc_product` is now `optional<string>`; unset means
  "derive from the version's flavor". (Was `= "wow"`.)
- Python/C#: `for_version` / `ForVersion` gained a `ClientVersion` overload
  next to the `Expansion` one — the only axis that can name a Classic client.
  Python dispatches through `canonical_version` (facade.hpp,
  `def_for_version_client_version_erased`, matching on each range's canonical);
  C# delegates to `ExpansionOf(version.FormatLineage)`
  (tools/gen_cs_format_facades.py).

## Gotchas hit

- `ClientVersion` gaining a member changes NTTP mangling — every welded symbol
  carrying a version now ends `..._wowlib_ClientFlavor_Retail`. Full rebuild.
- welder's C# aggregate constructor emits **no** default arguments, so anything
  generating `new ClientVersion(...)` must spell all five. dbdgen did
  (`tools/dbdgen/dbdgen/emit.py`); it now passes `ClientFlavor.Retail`.
- Python tests MUST run as `python -S` with
  `PYTHONPATH=build/bindings/bindings/python:$SITE`, or the editable `.pth`
  finder silently serves the stale installed `wowlib.abi3.so` and every new
  binding assertion "passes" against the old module. See [[bindings-notes]].
- `tests/python/typing/test_db_typed.mypy-testing` failed 3 cases here — NOT
  caused by this work, but not "pre-existing noise" either: the typed db stub
  merge was missing from `all` builds. Root-caused and fixed in the same
  branch; see [[bindings-notes]] "wowlib_pyi must be an ALL target".

## Tests

- `tests/unit/test_client_flavor.cpp` — consteval pins on the lineage table,
  storage kind, product codes, the content/format axis split, and that
  `WMO<classic_cata>` and `WMO<tww>` are the same type.
- `tests/python/test_classic.py` — the bound surface plus `detect()` against
  synthesized `.build.info` / `.flavor.info` fixtures.
- `tests/csharp/FormatFactoryTests.cs` — the `ForVersion(ClientVersion)`
  overload, including the two 4.4.0 builds landing on different engines.

No corpus test: no Classic client is installed locally. Adding one to the CI
server's `/root/WoWClients` would let the existing "every installed client
opens" integration sweep cover it for free.
