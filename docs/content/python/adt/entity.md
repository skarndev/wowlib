# ADT entity

The `ADT` tile class — the 256 terrain cells plus the tile-wide texture, model
and placement tables, unified across the physical files the tile is stored in.
Construct the concrete version with `ADT.for_version(expansion)`; the fields a
given client has are version dependent (MFBO since BC, water/texture-flags since
WotLK, the split-file `mamp` / `texture_params` since Cataclysm, the
`diffuse_texture_ids` / `height_texture_ids` FileDataID tables since 8.1). The
representative class below is the Battle-for-Azeroth-and-later layout with every
field.

::: wowlib.formats.adt.ADTBfaPlus
    options:
      show_root_heading: true
      show_root_toc_entry: true
      heading_level: 2
      members_order: source

## Structured liquid

Water is decoded to an editable form: `MH2OData` holds one `MapChunkLiquid` per
terrain cell, each a stack of `LiquidInstance` layers with their heightmap,
depth, texture-coordinate and per-tile "exists" data (the wire offsets are
re-derived on write). `MCLQData` is the deprecated pre-WotLK per-cell liquid.

::: wowlib.formats.adt.MH2OData
    options:
      heading_level: 3

::: wowlib.formats.adt.MapChunkLiquid
    options:
      heading_level: 3

::: wowlib.formats.adt.LiquidInstance
    options:
      heading_level: 3

::: wowlib.formats.adt.MCLQData
    options:
      heading_level: 3
