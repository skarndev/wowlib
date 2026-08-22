// The dbdgen-generated typed facades (WoWLib.Db.Tables.*): zero-interop
// wrappers over the generic Table with typed row properties, an indexer and
// allocation-free foreach.

using WoWLib;
using Db = WoWLib.Db;
using Versions = WoWLib.Versions;
using WoWLib.Db.Tables;
using Xunit;

namespace WoWLib.Tests;

public class DbFacadeTests
{
    [Fact]
    public void TypedFacadeWrapsTheGenericTable()
    {
        var map = MapWotlk.Open();
        Assert.Equal("Map", MapWotlk.TableName);
        Assert.Equal(0UL, map.RowCount);

        map.Table.AppendRow();
        var row = map[0];
        row.Id = 571;
        row.Directory = "Northrend";
        row.SetMapName("Northrend");

        Assert.Equal(571, map[0].Id);
        Assert.Equal("Northrend", map[0].Directory);
        Assert.Equal("Northrend", map[0].MapName());

        // The facade is a VIEW: the generic table sees the typed writes.
        Assert.Equal("Northrend",
                     map.Table.GetString(0, map.Table.ColumnIndex("directory"), 0));
    }

    [Fact]
    public void FacadeRoundTripMatchesTheGenericPath()
    {
        var map = MapWotlk.Open();
        map.Table.AppendRow();
        var row = map[0];  // the row struct is a view; writes go to the table
        row.Id = 1;
        row.Directory = "Kalimdor";

        var bytes = map.Write();

        using var generic = Db.Table.Open("Map", Versions.Global.Wotlk);
        generic.Read(bytes);
        Assert.Equal(bytes, generic.Write(Db.EncryptedPolicy.Preserve));
    }

    [Fact]
    public void ForeachEnumeratesTypedRows()
    {
        var map = MapWotlk.Open();
        for (var i = 0; i < 3; ++i)
        {
            map.Table.AppendRow();
            var row = map[(ulong)i];
            row.Id = i + 1;
        }
        long sum = 0;
        var count = 0;
        foreach (var row in map)
        {
            sum += row.Id;
            ++count;
        }
        Assert.Equal(3, count);
        Assert.Equal(6, sum);
    }
}
