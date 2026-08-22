# Database tables (DBC & DB2)

The client keeps its gameplay data — maps, spells, items, creatures — in
`DBFilesClient/`: **.dbc** tables pre-Cataclysm, **.db2** afterwards, across
six container generations (WDBC, WDB2, WDC1, WDC3, WDC4, WDC5). wowlib reads
and writes all of them through **one engine** whose schemas come from the
[WoWDBDefs](https://github.com/wowdev/WoWDBDefs) community data baked into
the library: open any of the ~1200 tables for any targeted client era, read
its rows, edit cells, write it back in the era's own on-disk format. The
format is sniffed from the file magic on read and re-emitted on write —
you never say "this is a WDC3".

## Opening a table, typed

Each language has a typed per-table surface over the generic engine:

=== "C++"

    ```cpp
    #include <wowlib/db/tables/map.hpp>   // generated, one header per table

    wowlib::db::tables::Map<wowlib::versions::wotlk> map;
    if (auto r = map.read(fs, wowlib::FileKey{"DBFilesClient/Map.dbc"}); !r)
      return report(r.error());

    for (auto& record : map.records)      // typed structs, plain std::vector
      if (record.instance_type == 0)
        use(record.id, record.directory);
    ```

=== "Python"

    ```python
    import wowlib
    from wowlib.db.tables import wotlk    # one module per era: vanilla … tww

    table = wotlk.Map()                   # schema-bound, era-exact columns
    table.read(fs, wowlib.FileKey("DBFilesClient\\Map.dbc"))

    for row in table:                     # rows read/write as attributes
        print(row.id, row.directory, row.map_name[0])   # enUS locale slot
    ```

    The era modules create their classes lazily as plain subclasses of the
    generic [`wowlib.db.Table`][wowlib.db.Table] — nothing is compiled per
    table — and the stubs type every column era-exactly, so your IDE
    completes `row.` with the wotlk-era `Map` schema.

=== "C#"

    ```csharp
    using WoWLib;
    using WoWLib.Db.Tables;               // dbdgen typed facades

    var map = MapWotlk.Open();
    map.Read(fs, new FileKey(@"DBFilesClient\Map.dbc"));

    foreach (var row in map)              // typed row views, no allocation
        Use(row.Id, row.Directory, row.MapName());
    ```

## The generic path

When the table name is dynamic (tooling, batch scripts), use the engine
directly — same behavior, runtime schema:

=== "C++"

    ```cpp
    #include <wowlib/db/dyn_table.hpp>

    auto table = wowlib::db::DynTable::open("Map", wowlib::versions::wotlk);
    table->read(fs, wowlib::FileKey{"DBFilesClient/Map.dbc"});
    for (std::size_t row = 0; row < table->row_count(); ++row)
      use(*table->get_string(row, *table->column_index("directory")));
    ```

=== "Python"

    ```python
    table = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    table.read(fs, wowlib.FileKey("DBFilesClient\\Map.dbc"))

    print(wowlib.db.table_names(wowlib.versions.wotlk))   # the era's catalog

    # Whole columns at once: zero-copy numpy for numerics.
    ids = table.column("id")              # numpy.int32, shape (rows,)
    directories = table.column("directory")
    ```

=== "C#"

    ```csharp
    using Db = WoWLib.Db;

    using var table = Db.Table.Open("Map", WoWLib.Versions.Global.Wotlk);
    table.Read(fs.ReadFile(@"DBFilesClient\Map.dbc"));
    for (ulong row = 0; row < table.RowCount; ++row)
        Use(table.GetString(row, table.ColumnIndex("directory"), 0));
    ```

## Editing and writing back

=== "C++"

    ```cpp
    map.records[0].directory = "EditedZone";
    // Serialize in the era's own container format — to bytes, or straight
    // back through the filesystem gateway (the project directory overlay).
    auto bytes = map.write();
    auto ok = map.write(fs, wowlib::FileKey{"DBFilesClient/Map.dbc"});
    ```

=== "Python"

    ```python
    table[0].directory = "EditedZone"     # rows are live views
    table.write(fs, wowlib.FileKey("DBFilesClient\\Map.dbc"))
    ```

=== "C#"

    ```csharp
    var row = map[0];                     // the row struct is a view
    row.Directory = "EditedZone";
    byte[] bytes = map.Write();
    ```

## Guarantees, era coverage, encryption

- **Round-trip**: WDBC/WDB2 (pre-Legion) writes are **byte-perfect**;
  WDC1..WDC5 writes are canonical re-encodes with a **semantic** guarantee
  (write → re-read → equal values) — the writer re-derives copy tables,
  pallets and common blocks the way the client's own tools do.
- **Era accuracy**: a table's schema differs per era (`map_name` is 8 locale
  slots in vanilla, 16 in TBC–WotLK, a single string from Cataclysm). The
  typed surfaces carry the era-exact shape; opening a table for an era it has
  no schema block for **raises** instead of guessing an adjacent layout.
- **Encrypted retail sections** (TACT): keyless sections are preserved
  verbatim and reported (`encrypted_sections`, `fully_decoded`); import keys
  on the filesystem to read them decrypted, or write with `EncryptedPolicy`
  to control what happens to what you cannot decrypt. See
  **[ClientDB concepts](../python/db/index.md)** for the full story.
