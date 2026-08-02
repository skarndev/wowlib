# Terrain chunks (MCNK)

A tile's terrain is 256 `MapChunk` chunks (`tile.chunks`, indexed
`y * 16 + x`) — the MCNK data, fully decoded: the 9 × 9 + 8 × 8 = 145 height
and normal grids, the texture layers with their decoded 64 × 64 alpha maps, the
shadow map, the doodad/object references and sound emitters, plus the
version-gated vertex colours, baked lighting, terrain materials and legacy
liquid. Every field documents **once**, badged with its MCNK sub-chunk and the
expansion range it exists in (a badge-less field is present in every supported
version).

`chunks` binds by reference, and every numeric grid is a zero-copy NumPy view:

```python
import numpy
chunk = tile.chunks[y * 16 + x]
heights = numpy.asarray(chunk.heights)       # 145 floats, writable in place
heights += 5.0                               # raise the whole chunk
alpha = chunk.alpha_maps[1]                  # layer 1's 64x64 blend map (bytes)
```

<!-- adt-legend -->

## The MapChunk chunk

::: wowlib.formats.adt.MapChunk
    options:
      heading_level: 3
      show_root_toc_entry: true

## Fields

<!-- adt-chunk-fields -->
