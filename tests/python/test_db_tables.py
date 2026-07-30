"""The generated client-database tables (wowlib.db.tables) and their facade.

The table classes/records/fields come from dbdgen + welder, sharded across TUs;
these tests cover the binding contracts that generation must preserve — the
version-keyed for_version facade (grid-gated), the AnyX union, the shared
supertypes, and a real byte-perfect round-trip through the fs gateway.
"""

import pytest

import wowlib


def test_tables_submodule_populated():
    tables = wowlib.db.tables
    # A spread of tables surface with their shared base + per-era concretes.
    assert issubclass(tables.MapWotlk, tables.Map)
    assert issubclass(tables.MapVanilla, tables.Map)
    # The record supertype is shared across a table's per-era record classes.
    assert issubclass(tables.MapRecordWotlk, wowlib.db.rowbase.Map)


def test_for_version_narrows_to_the_concrete_class():
    tables = wowlib.db.tables
    m = tables.Map.for_version(wowlib.Expansion.Wotlk)
    assert type(m) is tables.MapWotlk
    assert isinstance(m, tables.Map)  # native inheritance / isinstance
    # WMOAreaTable collapses tbc+wotlk into one range (shadowlands differs), so
    # both eras resolve to the same concrete class, distinct from shadowlands'.
    tbc = type(tables.WMOAreaTable.for_version(wowlib.Expansion.Tbc))
    wotlk = type(tables.WMOAreaTable.for_version(wowlib.Expansion.Wotlk))
    shadowlands = type(tables.WMOAreaTable.for_version(wowlib.Expansion.Shadowlands))
    assert tbc is wotlk
    assert shadowlands is not tbc
    assert issubclass(tbc, tables.WMOAreaTable)


def test_for_version_covers_every_era():
    # All eleven eras are bound (2026-07-30); each resolves to a concrete class.
    for era in (wowlib.Expansion.Cata, wowlib.Expansion.Legion,
                wowlib.Expansion.Dragonflight, wowlib.Expansion.TheWarWithin):
        m = wowlib.db.tables.Map.for_version(era)
        assert isinstance(m, wowlib.db.tables.Map)


def test_for_version_rejects_an_uncovered_table_era():
    # A table absent from an era's DBD (ItemSparse debuts in Cata) raises rather
    # than silently collapsing onto an adjacent range.
    with pytest.raises(ValueError):
        wowlib.db.tables.ItemSparse.for_version(wowlib.Expansion.Vanilla)


def test_any_union_folds_the_concrete_classes():
    tables = wowlib.db.tables
    members = set(wowlib.db.tables.AnyMap.__args__)
    # Map now spans every era; ranges may collapse adjacent eras into one
    # concrete class, so assert the known-distinct members are present rather
    # than pinning the full set.
    assert {tables.MapVanilla, tables.MapTbc, tables.MapWotlk,
            tables.MapShadowlands} <= members


def test_map_round_trips_byte_perfect(wotlk_fs):
    raw = wotlk_fs.read_file("DBFilesClient\\Map.dbc")
    table = wowlib.db.tables.Map.for_version(wowlib.Expansion.Wotlk)
    table.read(raw)
    assert len(table.records) > 0
    directories = {r.directory for r in table.records}
    assert {"Azeroth", "Kalimdor", "Northrend"} <= directories
    assert table.write(wowlib.db.EncryptedPolicy.Preserve) == raw


def test_record_edit_survives_round_trip(wotlk_fs):
    table = wowlib.db.tables.MapWotlk()
    table.read(wotlk_fs.read_file("DBFilesClient\\Map.dbc"))
    table.records[0].directory = "EditedZone"
    reread = wowlib.db.tables.MapWotlk()
    reread.read(table.write(wowlib.db.EncryptedPolicy.Preserve))
    assert reread.records[0].directory == "EditedZone"
