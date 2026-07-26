# WDT entity

The user-facing compound **`WDT`** — the assembly with everything baked in:
the main `.wdt` file plus every satellite file its era has, read and written
as one object. `WDT` is the abstract base; each expansion has a concrete
subclass (`WDTVanillaToMop`, `WDTWod`, …) exposing that version's layout —
the satellites appear as members only on the versions whose clients have
them (`occlusion`/`lights` since WoD, `fogs` since Legion 7.2.5,
`particulates` since BfA). Construct a concrete version with
`for_version(expansion)`; `read`/`write` speak the filesystem gateway, which
locates the satellites by the `{map}_occ.wdt` naming convention up to 8.1
and by the MPHD FileDataIDs after. A satellite file the map does not have
stays default-empty and is not written back.

The main file (`WDTRoot`) and the four satellites are documented
field-by-field, with expansion and FourCC badges, on
**[WDT main file](root.md)** and **[Satellites](satellites.md)**.

## The WDT assembly

::: wowlib.formats.wdt.WDT
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.wdt.WDTShadowlandsToDragonflight
    options:
      heading_level: 3
      show_root_toc_entry: true

!!! info "One layout per expansion, documented generically"
    The WDT layout is version-parametric — `WDT<version>` per expansion.
    Every version shares the same field *names*; a field simply exists only
    within its expansion range. The [WDT main file](root.md) and
    [Satellites](satellites.md) pages document that generically rather than
    repeating a dozen per-version class listings.

See the guide's **[Reading a map (WDT & WDL)](../../guide/maps.md)** for a
worked example.
