# Reading a WMO

A **WMO** (World Model Object) is WoW's format for large static models —
buildings, caves, cities. On disk it is a *root* file plus one or more *group*
files, each a sequence of `IFF`-style chunks. wowlib models both sides as
versioned entities: you get a concrete `WMO<expansion>` whose chunks are typed
structs, not opaque blobs.

!!! note "API shapes shown here"
    The exact class and method names are generated — see the
    **[Python API › WMO format](../python/wmo/index.md)** and the
    **[C++ reference](../reference.md)** for the authoritative signatures. The
    snippets below illustrate the intended shape.

## Python

```python
import wowlib
from wowlib.formats import wmo

data = open("World/wmo/Azeroth/Buildings/…/Building.wmo", "rb").read()
root = wmo.WMOWotlk.read(data)      # a Wrath-of-the-Lich-King WMO

for material in root.materials:
    ...

# Group geometry vectors come back as zero-copy NumPy views.
group = wmo.WMOWotlk.read_group(group_data)
verts = group.vertices              # numpy.ndarray, no copy
```

## C++

```cpp
#include <wowlib/formats/wmo/wmo.hpp>

auto root = wowlib::formats::wmo::WMO<Expansion::Wotlk>::read(bytes);
for (auto const& mat : root.materials()) { /* … */ }
```

The round-trip guarantee means `write(read(bytes))` reproduces the original
bytes for a well-formed file — see the formats architecture notes for how the
chunk framework preserves unknown/ordering data.
