# The generic table

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
