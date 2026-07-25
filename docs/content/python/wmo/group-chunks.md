# WMO group chunks

The typed wire structs of a WMO **group** file — geometry, render batches, liquid
grids and per-group lighting — plus the flag enums their fields carry. A struct
that changed across client versions (the render batch `WMOBatch`, the group
header `WMOGroupHeader`) **documents once**: one merged member walk under its
generic `⟨version⟩` name, each member badged with the expansion range it belongs
to (a badge-less member is identical in every version). Wire integer fields show
their on-disk width.

<!-- wmo-group-chunks -->
