# Terrain chunks

A tile's terrain is 256 `MapChunk` chunks (`tile.chunks`, indexed
`y * 16 + x`) — the MCNK data, fully decoded: the 9 × 9 + 8 × 8 = 145 height
and normal grids, the texture layers with their decoded 64 × 64 alpha maps, the
shadow map, the doodad/object references and sound emitters, plus version-gated
vertex colours (WotLK+), baked lighting and terrain materials (Cataclysm+) and
the legacy MCLQ liquid (up to and including WotLK).

`chunks` binds by reference, and every numeric grid is a zero-copy NumPy view:

```python
import numpy
chunk = tile.chunks[y * 16 + x]
heights = numpy.asarray(chunk.heights)       # 145 floats, writable in place
heights += 5.0                               # raise the whole chunk
alpha = chunk.alpha_maps[1]                  # layer 1's 64x64 blend map (bytes)
```

The representative class below is the Cataclysm-and-later chunk with every field;
pre-WotLK chunks instead carry `legacy_liquid` and lack `vertex_colors` /
`vertex_lighting` / `material_ids`.

::: wowlib.formats.adt.MapChunkCataPlus
    options:
      show_root_heading: true
      show_root_toc_entry: true
      heading_level: 2
      members_order: source
