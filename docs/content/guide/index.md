# Guide

The guide is the narrative introduction to wowlib — *why* each piece exists,
with runnable examples in every language the library speaks: **C++**,
**Python** and **C#**. Example blocks are tabbed; pick your language once and
every page follows. For the exhaustive *what*, see the generated
[Python API](../python/index.md) and [C++ API Reference](../reference.md).

- **[Getting started](getting-started.md)** — install a package or build from
  source.
- **[Reading client files (MPQ & CASC)](client-files.md)** — the `FileSystem`
  gateway: one API over both storage generations, paths and FileDataIDs,
  listfiles.
- **[World models (WMO)](wmo.md)** — the root + groups entity, typed chunks,
  zero-copy geometry.
- **[Models (M2)](m2.md)** — the compound animated-model entity and its
  external companion files.
- **[Maps (ADT, WDT & WDL)](maps.md)** — the tile table, the far heightmap,
  and editing terrain tiles.
- **[Database tables (DBC & DB2)](db.md)** — ~1200 client databases through
  one engine, typed per era.
- **[Writing version-agnostic code](version-agnostic.md)** — `for_version` /
  `ForVersion`, the family bases and typing unions: one tool, every client.
- **[Validating before writing](validation.md)** — the validation contracts
  and how to read a report.

!!! note "Work in progress"
    wowlib is early. This guide grows with the library; the API references are
    always current because they are generated from the sources on every build.
