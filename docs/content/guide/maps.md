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
