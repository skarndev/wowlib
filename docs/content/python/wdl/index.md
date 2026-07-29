# WDL format

The **WDL** low-resolution heightmap — one per map, drawn as the solid
background mountain silhouettes and usable as a minimap fallback. One chunked
file: a 64 × 64 tile offset table (MAOF) addressing one 17×17 + 16×16 int16
heightmap (MARE) per present tile, each followed by its hole mask (MAHO), plus
the era's low-resolution object placements and the Shadowlands+ sky scenes.

Pages:

- **[WDL entity](entity.md)** — the `WDL` class: every field with the
  expansion range it is available in (generated from the sources) and its
  chunk FourCC, plus how the tile table pairs with the per-tile chunks.
- **[Chunks](chunks.md)** — the wire structs (tile heightmaps, hole and ocean
  masks, LOD placements, sky scenes).

## The tile table and the per-tile chunks

The per-tile chunks are **repeating** members — plain lists that pair by
ordinal: the *i*-th heightmap belongs to the *i*-th **nonzero** `tile_offsets`
slot (row-major, y outer), and the *i*-th hole mask to the *i*-th heightmap.
The MAOF offset **values are derived** — every write restamps them from the
finished layout — so only the nonzero pattern is authored data:

```python
import numpy
from wowlib.formats import wdl

entity = wdl.WDL.for_version(wowlib.Expansion.Wotlk)
entity.tile_offsets.resize(64 * 64)
entity.tile_offsets[y * 64 + x] = 1         # any nonzero marks the tile present
entity.heightmaps.append(wdl.chunks.TileHeights())
entity.holes.append(wdl.chunks.TileHoles()) # all-or-nothing: one per heightmap
heights = entity.heightmaps[-1]
heights.outer[0] = 42                       # arrays bind by reference
numpy.asarray(heights.inner)[:] = my_16x16  # zero-copy int16 view, writable
data = entity.write()                       # offsets stamped, tiles interleaved
```

The sparse Legion+ ocean masks (MAOE) sit between a tile's heightmap and its
hole mask on disk; `ocean_mask_tiles()` reports which heightmap each mask
belongs to.

## Version-agnostic union

```python
from wowlib.formats.wdl import AnyWDL       # WDLVanilla | … | WDLTheWarWithin
```

See the guide's **[Reading a map (WDT & WDL)](../../guide/maps.md)** for a
worked example.
