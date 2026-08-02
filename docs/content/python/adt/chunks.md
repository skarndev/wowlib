# ADT chunk records

The typed wire structs of an ADT file — the per-chunk **MCNK header**
(`SMChunk`), the **texture layer** (`SMLayer`) and texture-flag / parameter /
material / colour-grading records, the terrain-normal and liquid-vertex records,
the **flying bounds** (`MFBOPlanes`) and the sound emitter. Each struct
backlinks the entity field that uses it; wire integer fields show their on-disk
width. The derived offset/size/count fields the serializer stamps are not
exposed.

The shared placement records ADT reuses (`SMMapObjDef` for MODF entries,
`SMDoodadDef` for MDDF entries) are documented with the
[common structures](../common.md).

::: wowlib.formats.adt.chunks
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 2
      members_order: source
