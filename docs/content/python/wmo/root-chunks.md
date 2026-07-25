# WMO root chunks

The typed wire structs of a WMO **root** file — materials, doodad sets and
definitions, lights, fog, portals and the header — plus the flag enums their
fields carry. A struct that changed across client versions **documents once**:
one merged member walk under its generic `⟨version⟩` name, each member badged
with the expansion range it belongs to (a badge-less member is identical in
every version). Wire integer fields show their on-disk width.

<!-- wmo-root-chunks -->
