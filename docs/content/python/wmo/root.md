# WMO root

The root file holds everything shared across a WMO's groups — materials,
doodads, lights, fog, portals and the group table. It is version-parametric:
the layout differs by client version, which wowlib models as one
`WMORoot<version>` per expansion (Python: `WMORootWotlk`, `WMORootShadowlands`,
…). Rather than repeat twelve near-identical class listings, this page
documents the **generic model**, shown generically as `WMORoot⟨version⟩`.

Each field carries its chunk **FourCC** (linking to wowdev.wiki). A field whose
availability is **version-restricted** also carries an expansion badge (both
ends inclusive); a field with **no badge exists in every supported version**.
The badges are generated from the C++ sources, so they cannot drift. Fields are
grouped into sections by area (materials, geometry, lights, …); a chunk that
was **removed** in a later client still appears in its section, its badge
showing where it went away.

<!-- wmo-legend -->

## The WMORoot base

::: wowlib.formats.wmo.root.WMORoot
    options:
      heading_level: 3
      show_root_toc_entry: true

## Fields

<!-- wmo-root-fields -->
