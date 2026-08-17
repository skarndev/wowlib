# ADT format

The **ADT** terrain tile — one of the 64 × 64 blocks a map is divided into,
each a 16 × 16 grid of terrain chunks (**MCNK**) with heights, normals, texture
layers and their alpha maps, water, shadows, and the doodad/WMO placements that
sit on it.

wowlib models the whole tile as **one versioned entity**, `ADT`, regardless of
how the client stores it on disk. Pre-Cataclysm a tile is a single `.adt`
file; Cataclysm split it into a root `.adt` plus `_tex0` / `_obj0` / `_obj1` /
`_lod` split files, *distributing* the same chunks (and each terrain chunk's MCNK
sub-chunks) across them. The reader loads every file of the tile and merges
them; the writer re-distributes on save — so **your code never branches on
which file a chunk lives in.** Adding a texture is the same call on a WotLK
tile and a Shadowlands tile.

Alpha maps are always presented **decoded** — a plain 64 × 64 8-bit edit
surface — whatever their on-disk encoding (4-bit, 8-bit, or RLE-compressed).
You tell `read()` / `write()` which on-disk encoding a map uses (its
`AlphaFormat`, from the WDT `MPHD` flags) — wowlib does not open the WDT for you.
Because those encodings and the offset tables are re-derived on write, ADT's
round-trip is **semantic** (an unmodified tile re-reads equal), not
byte-identical — like M2, unlike WMO/WDT/WDL.

Pages:

- **[ADT entity](entity.md)** — the `ADT` tile class, its fields with chunk +
  expansion badges, and the structured liquid records (`MH2OData`, `MCLQData`).
- **[Terrain chunks (MCNK)](map-chunk.md)** — the `MapChunk` chunk class:
  heights, normals, layers, alpha/shadow maps, references and the version-gated
  vertex colours, baked lighting and legacy liquid.
- **[Chunk records](chunks.md)** — the wire structs (the MCNK header, texture
  layers, liquid vertex records, flying bounds, sound emitters).

## Version-agnostic unions

```python
from wowlib.formats.adt import AnyADT        # ADTVanilla | … | ADTBfaPlus
from wowlib.formats.adt import AnyMapChunk   # MapChunkVanillaToTbc | … | MapChunkCataPlus
```

`ADT.for_version(expansion)` builds the concrete class for a client; every
`ADT*` class is a subclass of `ADT`, so `isinstance(tile, ADT)` and a
`tile: ADT` annotation both work.

See the guide's **[Terrain tiles (ADT)](../../guide/maps.md#terrain-tiles-adt)**
for a worked example.
