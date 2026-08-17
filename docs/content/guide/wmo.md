# World models (WMO)

A **WMO** (World Model Object) is WoW's format for large static models —
buildings, caves, whole cities. On disk it is a *root* file plus one group
file per interior/exterior piece (`Model_000.wmo` …), each an IFF-style chunk
stream. wowlib models the whole thing as **one versioned entity**: a
`WMO<version>` holds the typed root plus every group, and reading through the
filesystem gateway pulls the group files automatically.

Reads are **byte-perfect round-trips**: writing an unmodified WMO reproduces
the original bytes exactly, so a targeted edit never churns the rest of the
file.

## Reading a WMO from the client

=== "C++"

    ```cpp
    #include <wowlib/formats/wmo/wmo.hpp>

    wowlib::formats::wmo::WMO<wowlib::versions::wotlk> model;
    if (auto r = model.read(fs, wowlib::FileKey{
          "World/wmo/Dungeon/AZ_Subway/Subway.wmo"}); !r)
      return report(r.error());
    ```

=== "Python"

    ```python
    import wowlib
    from wowlib.formats import wmo

    model = wmo.WMO.for_version(wowlib.Expansion.Wotlk)
    model.read(fs, wowlib.FileKey("World/wmo/Dungeon/AZ_Subway/Subway.wmo"))
    ```

    `for_version` returns the concrete class covering that era
    (`WMOVanillaToWotlk` here — eras with identical layouts share one class),
    so everything after this line is fully typed.

=== "C#"

    ```csharp
    using wowlib;

    // Typed per-era factory (compile-time concrete class) …
    using var model = wowlib.Formats.Wmo.WMO.Wotlk();
    // … or version-agnostic, returning the family base:
    // using var model = wowlib.Formats.Wmo.WMO.ForVersion(Expansion.Wotlk);
    model.Read(fs, new FileKey("World/wmo/Dungeon/AZ_Subway/Subway.wmo"));
    ```

## Walking the data

The root carries the model-wide tables (materials, textures, doodad sets,
portals); each group carries its geometry in a `body`. Every vector is a
**live view** of the underlying C++ storage — mutations write through, and
numeric geometry arrays are zero-copy.

=== "C++"

    ```cpp
    for (const auto& material : model.root.materials)
      use(material.shader, material.blend_mode);

    for (const auto& group : model.groups)
      upload(group.body.vertices, group.body.normals, group.body.batches);
    ```

=== "Python"

    ```python
    for material in model.root.materials:
        use(material.shader, material.blend_mode)

    for group in model.groups:
        verts = numpy.asarray(group.body.vertices)   # zero-copy (N, 3) float32
        for batch in group.body.batches:
            ...
    ```

=== "C#"

    ```csharp
    var root = model.Root;
    for (int i = 0; i < root.Materials.Count; ++i)
        Use(root.Materials[i].Shader, root.Materials[i].BlendMode);

    var groups = model.Groups;
    for (int g = 0; g < groups.Count; ++g)
    {
        var body = groups[g].Body;          // live view into the entity
        Upload(body.Vertices, body.Normals, body.Batches);
    }
    ```

## Buffers instead of a client

You do not need an opened client — the entities also speak plain bytes.
In Python, `read`/`write` take any bytes-like or file-like source, with the
group streams passed alongside the root:

```python
model = wmo.WMO.for_version(wowlib.Expansion.Wotlk)
model.read(root_bytes, [group0_bytes, group1_bytes])

import io
sink, groups = io.BytesIO(), [io.BytesIO() for _ in model.groups]
model.write(sink, groups)
```

## Converting between versions

`convert()` re-targets a model to another era's layout — chunks gain or lose
fields according to the target's schema:

```python
retail_model = model.convert(wowlib.Expansion.Shadowlands)
```

The exact per-field version coverage is documented with expansion badges on
the **[WMO root](../python/wmo/root.md)** and
**[WMO group](../python/wmo/group.md)** pages; the C++ signatures live in the
[C++ reference](../reference.md).
