# WDT chunks

The typed wire structs of the WDT files — the MPHD map header, the MAIN tile
table and MAID FileDataID records of the main file, and the satellite files'
occlusion indices, lights, fogs and particulate volumes — plus the flag enums
their fields carry. A struct that changed across client versions **documents
once**: one merged member walk under its generic `⟨version⟩` name, each
member badged with the expansion range it belongs to. Wire integer fields
show their on-disk width.

The shared placement records the WDT reuses (`SMMapObjDef` for the global
WMO) are documented with the [common structures](../common.md).

## Main file

<!-- wdt-chunks-root -->

## Occlusion (`_occ.wdt`)

<!-- wdt-chunks-occ -->

## Lights (`_lgt.wdt`)

<!-- wdt-chunks-lgt -->

## Volumetric fogs (`_fogs.wdt`)

<!-- wdt-chunks-fogs -->

## Particulate volumes (`_mpv.wdt`)

<!-- wdt-chunks-mpv -->
