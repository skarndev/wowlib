# WMO fields & versions

A WMO is version-parametric: the layout differs by client version, which wowlib
models as one `WMO<version>` per expansion (Python: `WMOWotlk`, `WMOShadowlands`,
…). Rather than repeat twelve near-identical class listings, this page documents
the **generic WMO** — the version-agnostic base plus the per-version layout, shown
generically as `WMORoot⟨version⟩` / `WMOGroup⟨version⟩`. The field set is identical
across every expansion; only which fields are *active* varies, shown by the badges.

Each field carries its chunk **FourCC** (linking to wowdev.wiki). A field whose
availability is **version-restricted** also carries an expansion badge; a field
with **no badge exists in every supported version**, so the version-specific ones
stand out. The badges are generated from the C++ sources, so they cannot drift.

Fields are grouped into sections by area (materials, geometry, lights, …). A chunk
that was **removed** in a later client (its badge ends at a version) still appears
in its section — the badge shows where it went away.

<!-- wmo-legend -->

## Version-agnostic unions

Each family exposes an `Any…` **type alias** — the union of all its per-version
classes, bound as a real `types.UnionType` on its module (importable, and usable in
`isinstance` on Python ≥ 3.10). It is the runtime return type of
`for_version(expansion: Expansion)` and the natural annotation when your code
handles any version:

```python
from wowlib.formats.wmo.root import AnyWMORoot   # WMORootVanillaToWod | … | WMORootTheWarWithin

def process(root: AnyWMORoot) -> None:
    ...

root = WMORoot.for_version(pick_expansion())     # statically typed as AnyWMORoot
```

| Alias | Module | Union of |
|---|---|---|
| `AnyWMO` | `wowlib.formats.wmo` | every `WMO⟨version⟩` |
| `AnyWMORoot` | `wowlib.formats.wmo.root` | every `WMORoot⟨version⟩` |
| `AnyWMOGroup` | `wowlib.formats.wmo.group` | every `WMOGroup⟨version⟩` |
| `AnyWMOGroupBody` | `wowlib.formats.wmo.group` | every `WMOGroupBody⟨version⟩` |

The versioned chunk structs follow the same pattern — `AnyWMOGroupHeader` and
`AnyWMOBatch` in `wowlib.formats.wmo.group.chunks`.

## Root file

The root file holds everything shared across a WMO's groups — materials, doodads,
lights, fog, portals and the group table. `WMORoot` is the abstract base; the
per-version layout below is shown generically as `WMORoot⟨version⟩`.

::: wowlib.formats.wmo.root.WMORoot
    options:
      heading_level: 3
      show_root_toc_entry: true

<!-- wmo-root-fields -->

## Group file

A group file has two levels: the wrapper (`WMOGroup` — the format version and the
MOGP container) and the container body (`WMOGroupBody`), which holds the geometry.

::: wowlib.formats.wmo.group.WMOGroup
    options:
      heading_level: 3

::: wowlib.formats.wmo.group.WMOGroupDragonflightPlus
    options:
      heading_level: 3

::: wowlib.formats.wmo.group.WMOGroupBody
    options:
      heading_level: 3

<!-- wmo-group-fields -->
