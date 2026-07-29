# WDL entity

A map's low-resolution heightmap file as one versioned entity. It is
version-parametric: the layout differs by client version, which wowlib models
as one `WDL<version>` per expansion (Python: `WDLVanilla`,
`WDLTbcToWod`, `WDLLegionToBfa`, …). This page documents the **generic
model**, shown as `WDL⟨version⟩`.

Each field carries its chunk **FourCC** (linking to wowdev.wiki). A field
whose availability is **version-restricted** also carries an expansion badge
(both ends inclusive); a field with **no badge exists in every supported
version**. The badges are generated from the C++ sources, so they cannot
drift.

Reading speaks buffers or the filesystem gateway (`read(data)` /
`read(fs, key)`); writing returns bytes or stores through the project overlay
(`write()` / `write(fs, key)`). An entity read from a client file rewrites
byte-for-byte until modified; once tiles are added or removed, the write
rebuilds the stream — every non-tile chunk in canonical order, then each
tile's chunks interleaved — and restamps the MAOF offsets from the finished
layout.

<!-- wdl-legend -->

## The WDL base

::: wowlib.formats.wdl.WDL
    options:
      heading_level: 3
      show_root_toc_entry: true

## Fields

<!-- wdl-fields -->
