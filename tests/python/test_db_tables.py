"""The generic client-database surface (wowlib.db.Table).

One runtime-schema table class serves every table of every era — the schema
comes from the WoWDBDefs data baked into the library, not from generated
classes. These tests cover the binding contracts: catalog listing, schema
resolution per era, cell access in every column shape, the Record attribute
views, zero-copy numpy columns, and a real byte-perfect round-trip through
the fs gateway.
"""

import pytest

import wowlib


def test_table_names_lists_the_catalog():
    names = wowlib.db.table_names()
    assert "Map" in names
    assert "Spell" in names
    assert names == sorted(names)
    # Era filtering: ItemSparse debuts in Cataclysm.
    vanilla = wowlib.db.table_names(wowlib.versions.vanilla)
    assert "Map" in vanilla
    assert "ItemSparse" not in vanilla
    assert len(vanilla) < len(names)


def test_open_resolves_the_era_schema():
    table = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    assert table.row_count == 0
    assert table.name == "Map"
    assert table.version == wowlib.versions.wotlk
    # wotlk-era Map: known columns resolve, with era-true shapes.
    directory = table.column_index("directory")
    info = table.column_info(directory)
    assert info.type == wowlib.db.ColumnType.String
    map_name = table.column_info(table.column_index("map_name"))
    assert map_name.type == wowlib.db.ColumnType.LocString
    assert map_name.locale_count == 16  # post-TBC-2.1, pre-Cata
    # The same table at vanilla narrows the locale count.
    vanilla = wowlib.db.Table.open("Map", wowlib.versions.vanilla)
    v_name = vanilla.column_info(vanilla.column_index("map_name"))
    assert v_name.locale_count == 8


def test_open_rejects_unknown_tables_and_uncovered_eras():
    with pytest.raises(wowlib.TableUnknown):
        wowlib.db.Table.open("NoSuchTable", wowlib.versions.wotlk)
    # ItemSparse debuts in Cata: vanilla raises rather than silently
    # collapsing onto an adjacent range.
    with pytest.raises(wowlib.UnsupportedClientVersion):
        wowlib.db.Table.open("ItemSparse", wowlib.versions.vanilla)


def test_map_round_trips_byte_perfect(wotlk_fs):
    raw = wotlk_fs.read_file("DBFilesClient\\Map.dbc")
    table = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    table.read(raw)
    assert table.row_count > 0
    directories = {row.directory for row in table}
    assert {"Azeroth", "Kalimdor", "Northrend"} <= directories
    assert table.write(wowlib.db.EncryptedPolicy.Preserve) == raw


def test_record_views_read_and_write(wotlk_fs):
    table = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    table.read(wotlk_fs.read_file("DBFilesClient\\Map.dbc"))

    first = table[0]
    assert first.row_index == 0
    assert isinstance(first.directory, str)
    assert isinstance(first.id, int)
    # LocStrings surface as the full locale list; enUS is slot 0.
    names = first.map_name
    assert isinstance(names, list) and len(names) == 16
    # Attribute misses follow Python protocol (hasattr works).
    assert not hasattr(first, "no_such_column")
    assert "directory" in dir(first)

    first.directory = "EditedZone"
    reread = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    reread.read(table.write(wowlib.db.EncryptedPolicy.Preserve))
    assert reread[0].directory == "EditedZone"
    # Negative indexing follows sequence protocol.
    assert reread[-1].row_index == reread.row_count - 1


def test_cell_accessors_are_strict(wotlk_fs):
    table = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    table.read(wotlk_fs.read_file("DBFilesClient\\Map.dbc"))
    directory = table.column_index("directory")
    with pytest.raises(wowlib.SchemaMismatch):
        table.get_int(0, directory)
    with pytest.raises(wowlib.OffsetOutOfBounds):
        table.get_string(table.row_count, directory)
    with pytest.raises(wowlib.TableUnknown):
        table.column_index("no_such_column")


def test_numpy_columns_are_zero_copy(wotlk_fs):
    numpy = pytest.importorskip("numpy")
    table = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    table.read(wotlk_fs.read_file("DBFilesClient\\Map.dbc"))

    ids = table.column("id")
    assert ids.dtype == numpy.int32
    assert ids.shape == (table.row_count,)
    assert int(ids[0]) == table.get_int(0, table.column_index("id"))
    # The view aliases the store: an element write is live.
    row = table.row_count - 1
    ids[row] = 424242
    assert table.get_int(row, table.column_index("id")) == 424242

    # String columns come back as plain lists.
    directories = table.column("directory")
    assert isinstance(directories, list)
    assert "Azeroth" in directories


def test_era_modules_expose_typed_table_classes():
    from wowlib.db.tables import wotlk

    table = wotlk.Map()
    # A real subclass of the ONE generic engine — created lazily, no
    # compiled per-table code anywhere.
    assert isinstance(table, wowlib.db.Table)
    assert isinstance(table, wotlk.Map)
    assert type(table).__name__ == "Map"
    assert table.name == "Map"
    assert table.version == wowlib.versions.wotlk
    # First access caches the class in the module dict.
    assert wotlk.Map is wotlk.Map
    # Attribute misses follow Python protocol (hasattr works).
    assert not hasattr(wotlk, "NoSuchTable")
    with pytest.raises(AttributeError):
        wotlk.NoSuchTable  # noqa: B018
    # Era coverage matches the catalog: ItemSparse has no vanilla block (the
    # modern table's schema starts at the Legion era snap).
    assert not hasattr(wowlib.db.tables.vanilla, "ItemSparse")
    assert wowlib.db.tables.shadowlands.ItemSparse().name == "ItemSparse"
    # Discoverability: the era's tables list in dir().
    listing = dir(wotlk)
    assert "Map" in listing and "Spell" in listing
    assert "ItemSparse" not in dir(wowlib.db.tables.vanilla)


def test_era_modules_are_importable_by_dotted_path():
    import wowlib.db.tables.shadowlands as sl

    assert sl.Map().version == wowlib.versions.shadowlands


def test_era_table_reads_like_the_generic_one(wotlk_fs):
    table = wowlib.db.tables.wotlk.Map()
    table.read(wotlk_fs.read_file("DBFilesClient\\Map.dbc"))
    assert {"Azeroth", "Kalimdor"} <= {row.directory for row in table}


def test_rows_can_be_appended_and_erased():
    table = wowlib.db.Table.open("Map", wowlib.versions.wotlk)
    index = table.append_row()
    assert table.row_count == 1
    table.set_int(index, table.column_index("id"), 9000)
    assert table.find_by_id(9000) == index
    table.erase_row(index)
    assert table.row_count == 0
    with pytest.raises(wowlib.TableUnknown):
        table.find_by_id(9000)
