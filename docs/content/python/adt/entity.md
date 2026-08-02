# ADT entity

The `ADT` tile class — the 256 terrain chunks plus the tile-wide texture, model
and placement tables, unified across the split ADT files the tile is stored in.
Like the other versioned formats, every field below documents **once**, badged
with the chunk it serializes to and the expansion range it exists in (a
badge-less field is present in every supported version).

<!-- adt-legend -->

## The ADT tile

::: wowlib.formats.adt.ADT
    options:
      heading_level: 3
      show_root_toc_entry: true

## Fields

<!-- adt-tile-fields -->

## Structured liquid

Water is decoded to an editable form: `MH2OData` holds one `MapChunkLiquid` per
terrain chunk, each a stack of `LiquidInstance` layers with their heightmap,
depth, texture-coordinate and per-tile "exists" data (the wire offsets are
re-derived on write). `MCLQData` is the legacy per-chunk liquid (up to and
including WotLK — Outland tiles still use it).

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
