# WMO group

A group file holds one chunk of a WMO's geometry — vertices, normals, render
batches, collision and per-group lights, fog and liquid. It has two levels: the
wrapper (`WMOGroup` — the format version and the `MOGP` container) and the
container body (`WMOGroupBody`), which holds the geometry chunks. Both are
version-parametric, shown generically as `WMOGroup⟨version⟩` /
`WMOGroupBody⟨version⟩`.

Each field carries its chunk **FourCC** (linking to wowdev.wiki) plus an
expansion badge when it is version-restricted (both ends inclusive); a badge-less
field exists in every supported version. The badges are generated from the C++
sources, so they cannot drift.

<!-- wmo-legend -->

## The group wrapper

::: wowlib.formats.wmo.group.WMOGroup
    options:
      heading_level: 3
      show_root_toc_entry: true

## The WMOGroupBody base

::: wowlib.formats.wmo.group.WMOGroupBody
    options:
      heading_level: 3
      show_root_toc_entry: true

## Fields

<!-- wmo-group-fields -->
