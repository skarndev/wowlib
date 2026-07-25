# Common structures

The shared wire-level math and colour primitives (wowdev.wiki
[Common Types](https://wowdev.wiki/Common_Types)) used across every WoW file
format — vectors, matrices, planes, bounding volumes and colours — under their
established client names. All are trivially copyable with the exact on-disk
layout, so chunk payloads read straight into arrays of them; the numeric vectors
bind as zero-copy NumPy views (see [Containers](containers.md)).

::: wowlib.formats.common
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 2

## Chunk payload containers

Two member types from `wowlib.formats` own their whole chunk payload encoding
instead of mapping a wire struct: the decoded string-table block (MOTX, MOGN,
MODN, …) and the verbatim blob that preserves opaque or undocumented chunk
payloads byte-for-byte.

::: wowlib.formats.StringBlock
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.ChunkBlob
    options:
      heading_level: 3
      show_root_toc_entry: true
