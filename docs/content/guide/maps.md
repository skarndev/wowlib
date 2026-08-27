# Maps (ADT, WDT & WDL)

A map is three formats working together:

- the **WDT** says which of the 64 × 64 terrain tiles exist (or which single
  global WMO an instance shows), plus the map-wide satellites later expansions
  added (`_occ` occlusion, `_lgt` lights, `_fogs`, `_mpv`);
- the **WDL** is the low-resolution heightmap behind the distant mountain
  silhouettes;
- each existing tile is an **ADT** holding the terrain itself.

WDT and WDL round-trip **byte-perfect**; ADT (like M2) round-trips
**semantically** — it decodes alpha maps into a plain edit surface and
re-derives offset tables on write.

## The tile table (WDT) and the far heightmap (WDL)

=== "C++"

    ```cpp
    #include <wowlib/formats/wdl/wdl.hpp>
    #include <wowlib/formats/wdt/wdt.hpp>

    wowlib::formats::wdt::WDT<wowlib::versions::Wotlk> world;
    world.read(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth.wdt"});

    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x)
        if (world.root.tiles[y * 64 + x].flags & 1)
          ;  // this tile has an ADT

    wowlib::formats::wdl::WDL<wowlib::versions::Wotlk> low;
    low.read(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth.wdl"});
    ```

=== "Python"

    ```python
    import wowlib
    from wowlib.formats import wdt, wdl

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

=== "C#"

    ```csharp
    using WoWLib;

    using var world = WoWLib.Formats.WDT.WDT.Era.Wotlk();
    world.Read(fs, new FileKey("World/Maps/Azeroth/Azeroth.wdt"));

    using var low = WoWLib.Formats.WDL.WDL.Era.Wotlk();
    low.Read(fs, new FileKey("World/Maps/Azeroth/Azeroth.wdl"));
    ```

A WMO-only map (an instance) has the `uses_global_map_obj` header flag set,
its object in `root.global_wmo_name` / `root.global_wmo`, and no terrain
tiles. The WDL's MAOF offset table is **derived**: every write restamps the
64 × 64 offsets from the finished layout, so only the nonzero pattern (which
tiles exist) is authored data.

## Terrain tiles (ADT)

wowlib loads a tile as **one `ADT` entity** no matter how the client stores
it — a single pre-Cataclysm `.adt`, or the Cataclysm+ split of a root `.adt`
plus `_tex0` / `_obj0` / `_obj1` / `_lod` files. The reader merges every file;
the writer splits them back apart on save. **You never branch on which
physical file a chunk lives in.**

!!! note "You supply the alpha format"
    `read()` and `write()` take an `AlphaFormat` — the on-disk MCAL bit
    depth, decided by the map's **WDT `MPHD`** flags (`adt_has_big_alpha` /
    `adt_has_height_texturing` ⇒ `highres_8bit`, else `lowres_4bit`). wowlib
    does **not** open the WDT for you; read it yourself and pass the format.
    Decoded alpha maps are always presented as 64 × 64 regardless.

=== "C++"

    ```cpp
    #include <wowlib/formats/adt/adt.hpp>

    using wowlib::formats::adt::AlphaFormat;
    wowlib::formats::adt::ADT<wowlib::versions::Wotlk> tile;
    tile.read(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth_32_48.adt"},
              AlphaFormat::Lowres4Bit);

    for (auto& chunk : tile.chunks)          // 256 chunks, row-major
      for (float& h : chunk.heights)
        h += 10.0f;                          // raise the terrain

    tile.write(fs, wowlib::FileKey{"World/Maps/Azeroth/Azeroth_32_48.adt"},
               AlphaFormat::Lowres4Bit);
    ```

=== "Python"

    ```python
    import numpy
    import wowlib
    from wowlib.formats import adt

    tile = adt.ADT.for_version(wowlib.Expansion.Wotlk)
    # Azeroth ships 4-bit alpha in 3.3.5a (read its WDT MPHD flags to be sure).
    tile.read(fs, wowlib.FileKey("World/Maps/Azeroth/Azeroth_32_48.adt"),
              adt.AlphaFormat.lowres_4bit)

    # Raise the terrain: every chunk's height grid is a zero-copy NumPy view.
    for chunk in tile.chunks:                 # 256 chunks, row-major (y*16 + x)
        numpy.asarray(chunk.heights)[:] += 10.0

    tile.write(fs, wowlib.FileKey("World/Maps/Azeroth/Azeroth_32_48.adt"),
               adt.AlphaFormat.lowres_4bit)
    ```

=== "C#"

    ```csharp
    using WoWLib;
    using ADT = WoWLib.Formats.ADT;

    using var tile = ADT.ADT.Era.Wotlk();
    tile.Read(fs, new FileKey("World/Maps/Azeroth/Azeroth_32_48.adt"),
              ADT.AlphaFormat.lowres_4bit);

    foreach (var chunk in tile.Chunks)        // 256 chunks, row-major
    {
        var heights = chunk.Heights.AsSpan(); // zero-copy
        for (int i = 0; i < heights.Length; ++i)
            heights[i] += 10.0f;              // raise the terrain
    }

    tile.Write(fs, new FileKey("World/Maps/Azeroth/Azeroth_32_48.adt"),
               ADT.AlphaFormat.lowres_4bit);
    ```

### Adding a texture is version-agnostic

The point of the unified entity: the texture list is one chunk in the single
file pre-Cataclysm and a chunk in `_tex0` on a split tile — but the call is
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
```

Newer maps store their textures as FileDataIDs instead of names
(`diffuse_texture_ids` / `height_texture_ids`, 8.1+); `uses_texture_fdids`
records which scheme a tile uses. Water is decoded to an editable
`tile.water` (`MH2OData`), and the legacy per-chunk liquid (MCLQ, up to and
including WotLK — Outland tiles still use it) lives on `chunk.legacy_liquid`.

See the **[ADT](../python/adt/index.md)**, **[WDT](../python/wdt/index.md)**
and **[WDL](../python/wdl/index.md)** format pages for the authoritative
shapes and per-field version badges.

## Bulk access to per-vertex records (C#)

Per-vertex chunk data — heights, normals, colors — is hot-path material for
renderers, and element-wise access through the live views pays interop cost
per element. Every POD record type carries a blittable `Data` mirror of the
native layout; reinterpret the whole buffer as one span instead:

```csharp
using WoWLib;

foreach (var chunk in tile.Chunks)
{
    // ONE interop crossing for all 145 normals, zero allocations:
    var normals = chunk.Normals.AsDataSpan();   // == AsSpan<McnrEntry.Data>()
    for (int i = 0; i < normals.Length; i++)
        Emit(normals[i].Normal[0], normals[i].Normal[1], normals[i].Normal[2]);
}
```

The same applies to any scalar vector (`Indices`, `Heights`, …) through the
non-generic `AsSpan()`. Both spans are zero-copy views of C++ memory — valid
until a size-changing operation or `Dispose`, and writable (edits land
directly in the entity).

`AsSpan()` only *exists* for scalar/enum elements (it is a
`where T : unmanaged` extension), so calling it on a record- or
nested-container-element vector is a **compile error**, not a runtime throw
— `chunk.AlphaMaps` is a vector of per-layer buffers, so index a layer
first: `chunk.AlphaMaps[i].AsSpan()`.
