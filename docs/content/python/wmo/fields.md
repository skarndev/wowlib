# WMO fields & versions

A WMO is version-parametric: the layout differs by client version, which wowlib
models as one `WMO<version>` per expansion (Python: `WMOWotlk`, `WMOShadowlands`,
…). Rather than repeat twelve near-identical class listings, this page documents
the **generic WMO** — the version-agnostic base plus one representative concrete
version (**Wrath of the Lich King**), whose field set is shared by every version.

Each field carries its chunk **FourCC** (linking to wowdev.wiki). A field whose
availability is **version-restricted** also carries an expansion badge; a field
with **no badge exists in every supported version**, so the version-specific ones
stand out. The badges are generated from the C++ sources, so they cannot drift.

<!-- wmo-legend -->

## Root file

The root file holds everything shared across a WMO's groups — materials, doodads,
lights, fog, portals and the group table. `WMORoot` is the abstract base;
`WMORootWotlk` is the concrete Wrath layout shown here.

::: wowlib.formats.wmo.root.WMORoot
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.wmo.root.WMORootWotlk
    options:
      heading_level: 3

## Group file

A group file has two levels: the wrapper (`WMOGroup` — the format version and the
MOGP container) and the container body (`WMOGroupBody`), which holds the geometry.

::: wowlib.formats.wmo.group.WMOGroup
    options:
      heading_level: 3

::: wowlib.formats.wmo.group.WMOGroupWotlk
    options:
      heading_level: 3

::: wowlib.formats.wmo.group.WMOGroupBody
    options:
      heading_level: 3

::: wowlib.formats.wmo.group.WMOGroupBodyWotlk
    options:
      heading_level: 3
