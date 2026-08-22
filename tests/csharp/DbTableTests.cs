// The generic client-database surface (WoWLib.Database.Table) from C#: schema
// resolution per era, strict cell access, and an in-memory WDBC round-trip.
// Mirrors the client-free half of tests/python/test_db_tables.py.

using WoWLib;
using Db = WoWLib.Database;
using Versions = WoWLib.Versions;
using Xunit;

namespace WoWLib.Tests;

public class DbTableTests
{
    [Fact]
    public void OpenResolvesTheEraSchema()
    {
        using var table = Db.Table.Open("Map", Versions.Global.Wotlk);
        Assert.Equal(0UL, table.RowCount);

        var directory = table.ColumnIndex("directory");
        using var info = table.ColumnInfo(directory);
        Assert.Equal(Db.ColumnType.String, info.Type);

        using var mapName = table.ColumnInfo(table.ColumnIndex("map_name"));
        Assert.Equal(Db.ColumnType.LocString, mapName.Type);
        Assert.Equal(16, mapName.LocaleCount);  // post-TBC-2.1, pre-Cata

        // The same table at vanilla narrows the locale count.
        using var vanilla = Db.Table.Open("Map", Versions.Global.Vanilla);
        using var vName = vanilla.ColumnInfo(vanilla.ColumnIndex("map_name"));
        Assert.Equal(8, vName.LocaleCount);
    }

    [Fact]
    public void OpenRejectsUnknownTablesAndUncoveredEras()
    {
        Assert.Throws<WelderNativeException>(
            () => { using var t = Db.Table.Open("NoSuchTable", Versions.Global.Wotlk); });
        // ItemSparse debuts in Cata: vanilla raises rather than silently
        // collapsing onto an adjacent range.
        Assert.Throws<WelderNativeException>(
            () => { using var t = Db.Table.Open("ItemSparse", Versions.Global.Vanilla); });
        using var ok = Db.Table.Open("ItemSparse", Versions.Global.Shadowlands);
        Assert.Equal(0UL, ok.RowCount);
    }

    [Fact]
    public void CellAccessorsAreStrict()
    {
        using var table = Db.Table.Open("Map", Versions.Global.Wotlk);
        table.AppendRow();
        var directory = table.ColumnIndex("directory");
        // Type-strict: an int accessor on a string column raises.
        Assert.Throws<WelderNativeException>(() => { _ = table.GetInt(0, directory, 0); });
        // Bounds-strict: row past the end raises.
        Assert.Throws<WelderNativeException>(
            () => { _ = table.GetString(table.RowCount, directory, 0); });
        Assert.Throws<WelderNativeException>(
            () => { _ = table.ColumnIndex("no_such_column"); });
    }

    [Fact]
    public void WdbcRoundTripsByteIdentical()
    {
        using var table = Db.Table.Open("Map", Versions.Global.Wotlk);
        var row = table.AppendRow();
        table.SetInt(row, table.ColumnIndex("id"), 571, 0);
        table.SetString(row, table.ColumnIndex("directory"), "Northrend", 0);
        table.SetString(row, table.ColumnIndex("map_name"), "Northrend", 0);

        var bytes = table.Write(Db.EncryptedPolicy.Preserve);
        Assert.NotEmpty(bytes);

        using var reread = Db.Table.Open("Map", Versions.Global.Wotlk);
        reread.Read(bytes);
        Assert.Equal(1UL, reread.RowCount);
        Assert.Equal(571, reread.GetInt(0, reread.ColumnIndex("id"), 0));
        Assert.Equal("Northrend",
                     reread.GetString(0, reread.ColumnIndex("directory"), 0));
        // WDBC (the wotlk-era container) is a byte-identical round-trip.
        Assert.Equal(bytes, reread.Write(Db.EncryptedPolicy.Preserve));
    }
}
