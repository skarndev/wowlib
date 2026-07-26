# WDL chunks

The typed wire structs of a WDL file — the per-tile heightmaps, hole and
ocean masks, the Legion+ low-resolution placements and the Shadowlands+ sky
scenes. Wire integer fields show their on-disk width.

The shared placement records the WDL reuses (`SMMapObjDef` for MODF entries,
`SMDoodadDef` for MLDD) are documented with the
[common structures](../common.md).

<!-- wdl-chunks -->
