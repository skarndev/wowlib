# ClientDB (DBC & DB2)

The client-side databases — every file under `DBFilesClient/`: maps, spells,
items, creature display info, taxi nodes… `wowlib.db` reads and writes all of
them through **one runtime-schema table**: the schema for any table of any
supported client era comes from the community
[WoWDBDefs](https://github.com/wowdev/WoWDBDefs) definitions baked into the
library as data, so nothing per-table is generated or compiled — and every
era of every table is available from the same class.

wowlib speaks every on-disk database format a last-minor client ships:

| Clients | `.dbc` | `.db2` |
|---|---|---|
| Vanilla / TBC / WotLK | WDBC | — |
| Cata / MoP / WoD | WDBC | WDB2 |
| Legion | WDBC leftovers | WDC1 |
| BfA / Shadowlands | — | WDC3 |
| Dragonflight | — | WDC4 / WDC5 |
| The War Within | — | WDC5 |

Reads sniff the file magic, so you never name the format; a loaded table
re-emits the format it was read from, and a fresh table picks the era's native
one.

## The table surface

Everything lives on **[the generic table](tables.md)**:

- **[`Table.open(name, version)`][wowlib.db.Table.open]** resolves the
  era's schema by name (`wowlib.db.table_names()` lists what exists) and
  returns an empty table ready to `read` any file of that table.
- **`table[i]`** is a live [`Record`][wowlib.db.Record] view — columns read
  and write as attributes (`row.map_name`), shaped by the column.
- **`table.column(name)`** hands out a whole column: a zero-copy numpy view
  for numeric columns, lists for strings.
- Cell-level access (`get_int`/`set_string`/…) addresses
  `(row, column, element)` directly — the shape renderers and bulk editors
  want.

Columns keep their WoWDBDefs names, converted to `snake_case`. Pre-Cataclysm
localized-string columns expose one slot per client language (the `Record`
attribute is the `list[str]` of slots; `locstring_flags` rides alongside);
Cataclysm onwards they are plain strings.

```python
from wowlib import FileKey, versions
from wowlib.fs import FileSystem, FileSystemSettings
from wowlib.db import EncryptedPolicy, Table

settings = FileSystemSettings("/Games/WoW 3.3.5a", versions.wotlk)
with FileSystem.open(settings) as fs:
    table = Table.open("Map", versions.wotlk)
    table.read(fs, FileKey("DBFilesClient/Map.dbc"))

    for row in table:
        print(row.id, row.directory, row.map_name[0])   # slot 0 = enUS

    names = table[0].map_name
    names[0] = "Azeroth Reforged"
    table[0].map_name = names
    ids = table.column("id")                            # zero-copy numpy
    data = table.write(EncryptedPolicy.Preserve)        # bytes, or write(fs, key, …)
```

## Round-trip guarantees

- **WDBC / WDB2** (pre-Legion): byte-perfect — an unmodified table writes back
  identical bytes, string-block quirks included.
- **WDC1/3/4/5**: canonical re-encode — the write is a fresh, tightly packed
  encoding (bit widths, pallet/common compression and copy tables re-derived)
  that re-reads to equal values. Multi-section tables may reorder rows
  (records are id-keyed); single-section tables keep order.

## Encrypted sections

Modern `.db2` files can contain TACT-encrypted sections. Rows whose keys the
storage holds decode normally; rows behind **missing** keys are absent from
`records` and reported via `encrypted_sections` / `fully_decoded`. Register
keys with [`FileSystem.import_keys`][wowlib.fs.FileSystem.import_keys] to
decode them. On write, [`EncryptedPolicy`][wowlib.db.EncryptedPolicy] picks
between re-emitting a keyless file verbatim (`Preserve`, the default choice —
edits to decoded rows are **not** applied then) and writing a plain table of
just the decoded rows (`Drop`).

## Shared value types

The table classes themselves live on **[the generic table](tables.md)**;
this renders only what they share.

::: wowlib.db
    options:
      show_root_heading: false
      show_root_toc_entry: false
      members:
        - TableBase
        - LocString8
        - LocString16
        - EncryptedPolicy
        - EncryptedSection
