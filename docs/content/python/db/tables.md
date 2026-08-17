# Tables & records

## Typed per-era access

`wowlib.db.tables` holds one submodule per targeted era — `vanilla`, `tbc`,
`wotlk`, `cata`, `mop`, `wod`, `legion`, `bfa`, `shadowlands`,
`dragonflight`, `tww` — and each exposes **one class per table that era
defines**, schema-bound on construction:

```python
from wowlib.db.tables import wotlk

table = wotlk.Map()          # a real Table subclass, wotlk-era schema
table.read(fs, wowlib.FileKey("DBFilesClient\\Map.dbc"))
for row in table:
    print(row.id, row.directory, row.map_name[0])
```

The classes are created lazily at first attribute access as plain Python
subclasses of the generic [`Table`][wowlib.db.Table] — nothing is compiled
per table — and the accompanying stubs type every column **era-exactly**
(`wotlk.Map()[0].map_name` is `list[str]`; `cata.Map()[0].map_name` is
`str`). A table absent from an era is absent from its module:
`hasattr(wowlib.db.tables.vanilla, "ItemSparse")` is `False`.

`Table.open(name, version)` stays the dynamic spelling for runtime table
names; with a literal name it narrows to the union of that table's per-era
classes.

## The generic table

One class serves **every** client-database table of **every** era:
[`wowlib.db.Table`][wowlib.db.Table]. Its schema is resolved at runtime from
the WoWDBDefs data baked into the library — open any table by name for any
supported client version, read its rows, edit cells, and write it back in the
era's own on-disk format. There are no generated per-table classes; the
schema is data, not code.

::: wowlib.db.Table
    options:
      heading_level: 3
      show_root_toc_entry: true

## Row views

Indexing a table (`table[i]`) yields a live [`Record`][wowlib.db.Record]
view: columns read and write as **attributes** named after the WoWDBDefs
schema, shaped by the column — a scalar for scalar columns, a list for
arrays, a `list[str]` of locale slots for pre-Cataclysm localized strings.

::: wowlib.db.Record
    options:
      heading_level: 3
      show_root_toc_entry: true

## Column metadata

`table.column_info(i)` / `table.column_index(name)` describe the resolved
schema; `table.column(name)` hands the whole column out at once — a
**zero-copy numpy view** for numeric columns (exact dtype, element writes are
live), plain lists for string columns.

::: wowlib.db.Column
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.db.ColumnType
    options:
      heading_level: 3
      show_root_toc_entry: true

## Listing the catalog

::: wowlib.db.table_names
    options:
      heading_level: 3
      show_root_toc_entry: true
