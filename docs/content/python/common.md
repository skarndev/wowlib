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
