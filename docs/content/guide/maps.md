# Reading a map (WDT & WDL)

Every map ships a **WDT** — which of the 64 × 64 terrain tiles exist, or which
single global WMO an object-only map shows, plus the map-wide satellite files
later expansions added (`_occ` occlusion, `_lgt` lights, `_fogs` fogs, `_mpv`
particulate volumes) — and a **WDL**, the low-resolution heightmap behind the
distant mountain silhouettes. wowlib models both as versioned entities with
the byte-perfect round-trip guarantee.

!!! note "API shapes shown here"
    See the **[Python API › WDT format](../python/wdt/index.md)** and
    **[WDL format](../python/wdl/index.md)** for the authoritative signatures.

## Python

```python
import wowlib
from wowlib.formats import wdt, wdl

fs = wowlib.fs.FileSystem.open(wowlib.fs.FileSystemSettings(
    client_path="…/World of Warcraft 3.3.5a",
    version=wowlib.versions.wotlk))

# The WDT assembly pulls the main file and every satellite its era has.
world = wdt.WDT.for_version(wowlib.Expansion.Wotlk)
world.read(fs, wowlib.FileKey("World/Maps/Azeroth/Azeroth.wdt"))

for y in range(64):
    for x in range(64):
        if world.root.tiles[y * 64 + x].flags & 1:
            ...          # this tile has an ADT

# The WDL heightmaps pair with the nonzero tile_offsets slots by ordinal.
low = wdl.WDL.for_version(wowlib.Expansion.Wotlk)
low.read(fs, wowlib.FileKey("World/Maps/Azeroth/Azeroth.wdl"))
for heights, holes in zip(low.heightmaps, low.holes):
    ...                  # heights.outer: 17x17 int16, heights.inner: 16x16
```

A WMO-only map (an instance) has the `uses_global_map_obj` header flag set,
its object in `root.global_wmo_name` / `root.global_wmo`, and no terrain
tiles.

## C++

```cpp
#include <wowlib/formats/wdl/wdl.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

wowlib::formats::wdt::WDT<wowlib::versions::wotlk> world;
world.read(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth.wdt"});

wowlib::formats::wdl::WDL<wowlib::versions::wotlk> low;
low.read(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth.wdl"});
```

The WDL's MAOF offset table is **derived**: on every write the library
restamps the 64 × 64 offsets from the finished layout, so only the nonzero
pattern (which tiles exist) is authored data — see the
[WDL format page](../python/wdl/index.md) for the editing recipe.

## Editing terrain (ADT)

Each existing terrain tile is an **ADT**. Where the WDT says *which* tiles
exist, the ADT holds the terrain itself: 16 × 16 chunks of heights and normals,
the texture layers blended through their alpha maps, water, and the doodads and
WMOs placed on the tile. wowlib loads a tile as **one `ADT` entity** no matter
how the client stores it — a single pre-Cataclysm `.adt`, or the Cataclysm+
split of a root `.adt` plus `_tex0` / `_obj0` / `_obj1` / `_lod` split files. The
reader merges every file; the writer splits them back apart on save.

!!! note "You supply the alpha format"
    `read()` and `write()` take an `AlphaFormat` — the on-disk MCAL bit depth,
    decided by the map's **WDT `MPHD`** flags (`adt_has_big_alpha` /
    `adt_has_height_texturing` ⇒ `highres_8bit`, else `lowres_4bit`). wowlib does
    **not** open the WDT for you; read it yourself and pass the format. wowlib
    always presents decoded 64 × 64 maps regardless.

!!! note "Semantic round-trip, not byte-perfect"
    ADT decodes alpha maps to a plain 64 × 64 edit surface and re-derives the
    offset tables, so an unmodified tile re-reads **equal** but not
    byte-identical (like [M2](../python/m2/index.md), unlike WDT/WDL). See the
    **[ADT format](../python/adt/index.md)** pages for the authoritative shapes.

```python
import numpy
import wowlib
from wowlib.formats import adt

fs = wowlib.fs.FileSystem.open(wowlib.fs.FileSystemSettings(
    client_path="…/World of Warcraft 3.3.5a",
    version=wowlib.versions.wotlk))

tile = adt.ADT.for_version(wowlib.Expansion.Wotlk)
# Azeroth ships 4-bit alpha in 3.3.5a (read its WDT MPHD flags to be sure).
tile.read(fs, wowlib.FileKey("World/Maps/Azeroth/Azeroth_32_48.adt"),
          adt.AlphaFormat.lowres_4bit)

# Raise the terrain: every chunk's height grid is a zero-copy NumPy view.
for chunk in tile.chunks:                     # 256 chunks, row-major (y*16 + x)
    numpy.asarray(chunk.heights)[:] += 10.0
```

### Adding a texture is version-agnostic

The point of the unified entity: **you never branch on which physical file a
chunk lives in.** On a pre-Cataclysm tile the texture list is one chunk in the
single file; on a Shadowlands tile it is a chunk in `_tex0` — but the call is
identical, because the serializer routes it:

```python
# Pre-Cataclysm: MTEX names.
wotlk = adt.ADT.for_version(wowlib.Expansion.Wotlk)
wotlk.read(fs, wowlib.FileKey("World/Maps/Azeroth/Azeroth_32_48.adt"),
           adt.AlphaFormat.lowres_4bit)
layer_index = len(wotlk.textures.entries())
wotlk.textures.add("tileset\\generic\\black.blp")

# The exact same shape on a split (Cataclysm+) tile — kultiras is a big-alpha map.
sl = adt.ADT.for_version(wowlib.Expansion.Shadowlands)
sl.read(fs_927, wowlib.FileKey("world/maps/kultiras/kultiras_31_29.adt"),
        adt.AlphaFormat.highres_8bit)
sl.textures.add("tileset\\generic\\black.blp")

# Point a chunk's second layer at the new texture and paint its alpha map.
chunk = wotlk.chunks[0]
chunk.layers[1].texture_id = layer_index      # 0-based index into the texture list
chunk.layers[1].flags |= int(adt.chunks.LayerFlags.use_alpha_map)
numpy.asarray(chunk.alpha_maps[1])[:] = my_64x64_blend  # 0 = base, 255 = this layer

# Write with the same format you read (or the one you want on disk).
tile.write(fs, wowlib.FileKey("World/Maps/Azeroth/Azeroth_32_48.adt"),
           adt.AlphaFormat.lowres_4bit)
```

Newer maps store their textures as FileDataIDs instead of names
(`diffuse_texture_ids` / `height_texture_ids`, 8.1+); `uses_texture_fdids`
records which scheme a tile uses. Water is decoded to an editable
`tile.water` (`MH2OData`), and the legacy per-chunk liquid (MCLQ, up to and
including WotLK — Outland tiles still use it) lives on `chunk.legacy_liquid`.

## C++

```cpp
#include <wowlib/formats/adt/adt.hpp>

wowlib::formats::adt::ADT<wowlib::versions::wotlk> tile;
using wowlib::formats::adt::AlphaFormat;
tile.read(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth_32_48.adt"},
          AlphaFormat::lowres_4bit);
for (auto& chunk : tile.chunks)
  for (float& h : chunk.heights)
    h += 10.0f;
tile.write(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth_32_48.adt"},
           AlphaFormat::lowres_4bit);
```
