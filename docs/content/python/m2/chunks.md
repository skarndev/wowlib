# M2 chunks — the Legion+ shell

From Legion (7.0.1) on, the on-disk `.m2` is a chunked stream: the MD20 image
moves into the `MD21` chunk — the [M2 root](root.md), offsets intact — joined
by satellite chunks: FileDataID references, extended particle data,
parent-model overrides, inline physics. Chunk ids are **not** reversed on disk,
unlike every other WoW chunk format. Each field below carries its chunk
**FourCC** (linking to wowdev.wiki) plus an expansion badge when the chunk is
version-restricted (both ends inclusive); an untouched shell rewrites
byte-for-byte.

<!-- m2-legend -->

## The M2ChunkedFile base

::: wowlib.formats.m2.chunked.M2ChunkedFile
    options:
      heading_level: 3
      show_root_toc_entry: true

## Chunks

<!-- m2-shell-fields -->
