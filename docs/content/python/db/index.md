# ClientDB (DBC & DB2)

The client-side databases — every file under `DBFilesClient/`: maps, spells,
items, creature display info, taxi nodes… `wowlib.db` reads and writes all of
them as **typed tables**: each table is a generated class whose rows carry one
typed property per column, generated from the community
[WoWDBDefs](https://github.com/wowdev/WoWDBDefs) definitions for every
supported client era.

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

Every table follows one shape, documented on
**[Tables & records](tables.md)** with `Map` as the representative:

- **`wowlib.db.tables.X`** — the version-agnostic base:
  `X.for_version(expansion)` builds the concrete per-era class; every era class
  is a subclass, so `isinstance(t, X)` and a `t: X` annotation work.
- **`wowlib.db.tables.XRecord⟨era⟩`** — one typed row; `table.records` is a
  plain mutable list of them, and `write()` serializes exactly that list.
- **`wowlib.db.rowbase.X`** — an era-agnostic row supertype every
  `XRecord⟨era⟩` inherits, for annotating code that handles any era's rows.

Columns keep their WoWDBDefs names, converted to `snake_case`. Pre-Cataclysm
localized-string columns decode to [`LocString8`][wowlib.db.LocString8] /
[`LocString16`][wowlib.db.LocString16] (one slot per client language);
Cataclysm onwards they are plain strings.

```python
from wowlib import Expansion, FileKey, Locale, versions
from wowlib.fs import FileSystem, FileSystemSettings
from wowlib.db import EncryptedPolicy
from wowlib.db.tables import Map

settings = FileSystemSettings("/Games/WoW 3.3.5a", versions.wotlk)
with FileSystem.open(settings) as fs:
    table = Map.for_version(Expansion.Wotlk)
    table.read(fs, FileKey("DBFilesClient/Map.dbc"))

    for row in table.records:
        print(row.id, row.directory, row.map_name.at(Locale.enUS))

    table.records[0].map_name.set(Locale.enUS, "Azeroth Reforged")
    data = table.write(EncryptedPolicy.Preserve)      # bytes, or write(fs, key, …)
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

::: wowlib.db
    options:
      show_root_heading: false
      show_root_toc_entry: false
