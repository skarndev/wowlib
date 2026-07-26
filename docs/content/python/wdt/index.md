# WDT format

The **WDT** map description — one per map, saying exactly which of the 64 × 64
terrain tiles exist (or which single global WMO an object-only map shows), and
carrying the map-wide satellite files later expansions added. `WDT` is the
compound user-facing class (the main `.wdt` file plus its era's
`_occ`/`_lgt`/`_fogs`/`_mpv` satellites, baked into one object); each expansion
has a concrete subclass (`WDTWod`, `WDTShadowlandsToDragonflight`, …) exposing
that version's layout.

Pages:

- **[WDT entity](entity.md)** — the user-facing compound `WDT` (the main file
  with every satellite baked in), read/written through the filesystem gateway.
- **[WDT main file](root.md)** — the main file: every field with the expansion
  range it is available in (generated from the sources, so the ranges never
  drift) and its chunk FourCC.
- **[Satellites](satellites.md)** — the `_occ` occlusion, `_lgt` lights,
  `_fogs` volumetric-fog and `_mpv` particulate-volume files, each field with
  its FourCC and expansion badges.
- **[Chunks](chunks.md)** — the wire structs used across all five files.

## Version-agnostic unions

Each family exposes an `Any…` **type alias** — the union of its per-version
classes (a single-range family's alias is its one class), bound on its module
and importable:

```python
from wowlib.formats.wdt import AnyWDT

def process(wdt: AnyWDT) -> None:
    ...

wdt = WDT.for_version(pick_expansion())     # statically typed as AnyWDT
```

| Alias | Module | Union of |
|---|---|---|
| `AnyWDT` | `wowlib.formats.wdt` | every `WDT⟨version⟩` |
| `AnyWDTRoot` | `wowlib.formats.wdt.root` | every `WDTRoot⟨version⟩` |
| `AnyWDTHeader` | `wowlib.formats.wdt.root.chunks` | every `WDTHeader⟨version⟩` |
| `AnyWDTOcclusion` | `wowlib.formats.wdt.occlusion` | the one `WDTOcclusionWodPlus` |
| `AnyWDTLights` | `wowlib.formats.wdt.lights` | every `WDTLights⟨version⟩` |
| `AnyWDTFogs` | `wowlib.formats.wdt.fogs` | every `WDTFogs⟨version⟩` |
| `AnyWDTParticulates` | `wowlib.formats.wdt.mpv` | the one `WDTParticulatesBfaPlus` |

See the guide's **[Reading a map (WDT & WDL)](../../guide/maps.md)** for a
worked example.
