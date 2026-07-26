# WDT satellites

The per-map satellite files later expansions added beside the main `.wdt`,
each a chunked entity of its own on the `WDT` assembly: the `_occ` occlusion
heightmaps and `_lgt` placed lights (WoD+), the `_fogs` volumetric fogs
(Legion 7.2.5+) and the `_mpv` particulate volumes (BfA+). Fields carry the
same FourCC and expansion badges as the main file's page; a missing satellite
file simply leaves its entity default-empty.

## Occlusion (`_occ.wdt`)

Per-tile low-resolution heightmaps (the same 17×17 + 16×16 int16 grid as the
[WDL](../wdl/entity.md) tiles) that occlude distant geometry: the MAOI index
locates each tile's grid inside the packed MAOH block.

::: wowlib.formats.wdt.occlusion.WDTOcclusion
    options:
      heading_level: 3
      show_root_toc_entry: true

<!-- wdt-occ-fields -->

## Lights (`_lgt.wdt`)

Freely placed map lights — point lights under lamp posts and the like, plus
Legion's spot lights, light-cookie textures and animations. The point-light
record was replaced twice: MPLT (WoD) → MPL2 (Legion) → MPL3 (Shadowlands).

::: wowlib.formats.wdt.lights.WDTLights
    options:
      heading_level: 3
      show_root_toc_entry: true

<!-- wdt-lgt-fields -->

## Volumetric fogs (`_fogs.wdt`)

Placed fog volumes. The file appears (empty) in Legion 7.2.5; the first VFOG
content ships with BfA 8.0.1, and format version 2 (The War Within) adds the
VFEX extension records.

::: wowlib.formats.wdt.fogs.WDTFogs
    options:
      heading_level: 3
      show_root_toc_entry: true

<!-- wdt-fogs-fields -->

## Particulate volumes (`_mpv.wdt`)

Weather particulate volumes in repeated PVMI/PVPD/PVBD groups — the i-th
elements of the three lists belong together, and the on-disk group interleave
round-trips through the journal. The PVMI record layout is keyed on the
file's own version payload, so it is kept opaque.

::: wowlib.formats.wdt.mpv.WDTParticulates
    options:
      heading_level: 3
      show_root_toc_entry: true

<!-- wdt-mpv-fields -->
