# Tables & records

`wowlib.db.tables` holds **every** client-database table — around 1200
families, each generated per era from WoWDBDefs. Documenting them all would
bury the reference, and they all share one shape, so this page documents the
representative **`Map`** family in full; substitute any table name
(`Spell`, `ItemSparse`, `TaxiNodes`, …) and the same structure applies.
The per-table column documentation lives in your editor: the shipped stubs
carry every table, every era, every column docstring.

## The table family

`Map` is the version-agnostic base: construct the concrete era class with
`for_version(expansion)`. The concrete classes cover *ranges* of eras — one
class per distinct layout, exactly as the layouts evolved.

::: wowlib.db.tables.Map
    options:
      heading_level: 3
      show_root_toc_entry: true

## The table API

Every concrete table class has the same members — a `records` list, the
filesystem-aware `read`/`write` pair, the preserved string block and the
encryption report. Rendered from the newest `Map` era class; only the record
type in `records` differs per era.

::: wowlib.db.tables.MapTheWarWithin
    options:
      heading_level: 3
      show_root_toc_entry: true

## Records

One typed row class per layout range, all inheriting the era-agnostic
`wowlib.db.rowbase.Map` supertype. The family documents **once** below, the
way wowdev.wiki lists a versioned struct: one merged member walk, each member
badged with the era range that declares it — a badge-less member exists in
every era the table covers. A member whose type changed across eras appears
once per layout, each entry badged.

<!-- db-map-records -->
