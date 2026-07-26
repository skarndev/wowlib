# WDT main file

The main `.wdt` file holds the map-wide header (MPHD), the 64 × 64 tile table
(MAIN) saying which tiles have terrain, since 8.1 the per-tile FileDataID
table (MAID), and — for WMO-only maps — the single global map object
(MWMO/MODF). It is version-parametric: the layout differs by client version,
which wowlib models as one `WDTRoot<version>` per expansion (Python:
`WDTRootVanillaToLegion`, `WDTRootBfa`, `WDTRootShadowlandsPlus`). This page
documents the **generic model**, shown as `WDTRoot⟨version⟩`.

Each field carries its chunk **FourCC** (linking to wowdev.wiki). A field
whose availability is **version-restricted** also carries an expansion badge
(both ends inclusive); a field with **no badge exists in every supported
version**. The badges are generated from the C++ sources, so they cannot
drift.

<!-- wdt-legend -->

## The WDTRoot base

::: wowlib.formats.wdt.root.WDTRoot
    options:
      heading_level: 3
      show_root_toc_entry: true

## Fields

<!-- wdt-root-fields -->
