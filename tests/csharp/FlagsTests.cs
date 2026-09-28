// Bitmask enums ([[=welder::flags]]): flag fields are typed as their enums
// rather than raw uints, the enums carry [System.Flags], and combined /
// undocumented bit values survive the crossing — real client files ship both.

using WoWLib;
using Formats = WoWLib.Formats;
using GroupChunks = WoWLib.Formats.WMO.Group.Chunks;
using Xunit;

namespace WoWLib.Tests;

public class FlagsTests
{
    [Fact]
    public void FlagEnumsCarrySystemFlags()
    {
        Assert.True(typeof(GroupChunks.GroupFlags)
                        .IsDefined(typeof(System.FlagsAttribute), false));
        Assert.True(typeof(Formats.ADT.Chunks.LayerFlags)
                        .IsDefined(typeof(System.FlagsAttribute), false));
        Assert.True(typeof(Formats.M2.Root.GlobalFlags)
                        .IsDefined(typeof(System.FlagsAttribute), false));
        // A non-bitmask enum stays unmarked.
        Assert.False(typeof(Formats.ADT.AlphaFormat)
                         .IsDefined(typeof(System.FlagsAttribute), false));
    }

    [Fact]
    public void FlagFieldsAreEnumTypedAndCombinable()
    {
        using var body =
            Formats.WMO.Group.WMOGroupBody.ForVersion(Expansion.Wotlk);
        // The field is the enum, not a uint; combination is idiomatic and
        // reads back intact through the hoisted family-base header.
        body.Header.Flags =
            GroupChunks.GroupFlags.Exterior |
            GroupChunks.GroupFlags.HasVertexColors;
        Assert.True(body.Header.Flags.HasFlag(GroupChunks.GroupFlags.Exterior));
        Assert.Equal("HasVertexColors, Exterior",
                     body.Header.Flags.ToString());

        // Undocumented bits (real files carry them) survive the round trip.
        body.Header.Flags = (GroupChunks.GroupFlags)0x80000004u;
        Assert.Equal(0x80000004u, (uint)body.Header.Flags);
    }
}
