// The per-era format factories (the C# for_version): the statics merged onto
// the welded family bases by tools/gen_cs_format_facades.py. These lock the
// tester-reported gap — version-agnostic construction must exist and return
// the era's covering range class.

using WoWLib;
using Versions = WoWLib.Versions;
using Xunit;

namespace WoWLib.Tests;

public class FormatFactoryTests
{
    [Theory]
    // The ADT assembly ranges (bindings/instantiations/adt_ranges.hpp):
    // Vanilla / Tbc / Wotlk / CataToLegion / BfaPlus.
    [InlineData(Expansion.Vanilla, typeof(WoWLib.Formats.ADT.ADTVanilla))]
    [InlineData(Expansion.Tbc, typeof(WoWLib.Formats.ADT.ADTTbc))]
    [InlineData(Expansion.Wotlk, typeof(WoWLib.Formats.ADT.ADTWotlk))]
    [InlineData(Expansion.Cata, typeof(WoWLib.Formats.ADT.ADTCataToLegion))]
    [InlineData(Expansion.Mop, typeof(WoWLib.Formats.ADT.ADTCataToLegion))]
    [InlineData(Expansion.Wod, typeof(WoWLib.Formats.ADT.ADTCataToLegion))]
    [InlineData(Expansion.Legion, typeof(WoWLib.Formats.ADT.ADTCataToLegion))]
    [InlineData(Expansion.Bfa, typeof(WoWLib.Formats.ADT.ADTBfaPlus))]
    [InlineData(Expansion.Shadowlands, typeof(WoWLib.Formats.ADT.ADTBfaPlus))]
    [InlineData(Expansion.Dragonflight, typeof(WoWLib.Formats.ADT.ADTBfaPlus))]
    [InlineData(Expansion.TheWarWithin, typeof(WoWLib.Formats.ADT.ADTBfaPlus))]
    public void AdtForVersionCoversEveryEraWithTheCoveringRange(
        Expansion era, System.Type expected)
    {
        using var adt = WoWLib.Formats.ADT.ADT.ForVersion(era);
        Assert.Equal(expected, adt.GetType());
    }

    [Fact]
    public void PerEraStaticsReturnTheConcreteRangeClass()
    {
        // Compile-time typing is the point: no cast on the concrete.
        using WoWLib.Formats.ADT.ADTWotlk adt = WoWLib.Formats.ADT.ADT.Era.Wotlk();
        WoWLib.Formats.ADT.ADT asBase = adt;
        Assert.Same(adt, asBase);
    }

    [Fact]
    public void EveryFamilyBaseFactoryConstructs()
    {
        using var wmo = WoWLib.Formats.WMO.WMO.ForVersion(Expansion.Wotlk);
        using var m2 = WoWLib.Formats.M2.M2.ForVersion(Expansion.Legion);
        using var wdt = WoWLib.Formats.WDT.WDT.ForVersion(Expansion.Shadowlands);
        using var wdl = WoWLib.Formats.WDL.WDL.ForVersion(Expansion.Vanilla);
        using var root = WoWLib.Formats.WMO.Root.WMORoot.ForVersion(Expansion.Tbc);
        Assert.IsAssignableFrom<WoWLib.Formats.WMO.WMO>(wmo);
        Assert.IsAssignableFrom<WoWLib.Formats.M2.M2>(m2);
        Assert.IsAssignableFrom<WoWLib.Formats.WDT.WDT>(wdt);
        Assert.IsAssignableFrom<WoWLib.Formats.WDL.WDL>(wdl);
        Assert.IsAssignableFrom<WoWLib.Formats.WMO.Root.WMORoot>(root);
    }

    [Fact]
    public void FamilyBasesCarryTheFsVerbs()
    {
        // Version-agnostic code is the point of ForVersion: the base must be
        // able to Read/Write without a downcast. Method-group assignments
        // compile-lock the base verbs' signatures; a bare base instance (no
        // concrete era) reports itself instead of nulling into a backend.
        using var adt = WoWLib.Formats.ADT.ADT.ForVersion(Expansion.Wotlk);
        System.Action<WoWLib.Fs.FileSystem, FileKey,
                      WoWLib.Formats.ADT.AlphaFormat> adtRead = adt.Read;
        System.Action<WoWLib.Fs.FileSystem, FileKey,
                      WoWLib.Formats.ADT.AlphaFormat> adtWrite = adt.Write;
        Assert.NotNull(adtRead);
        Assert.NotNull(adtWrite);

        using var wmo = WoWLib.Formats.WMO.WMO.ForVersion(Expansion.Vanilla);
        using var m2 = WoWLib.Formats.M2.M2.ForVersion(Expansion.Legion);
        using var wdt = WoWLib.Formats.WDT.WDT.ForVersion(Expansion.Cata);
        using var wdl = WoWLib.Formats.WDL.WDL.ForVersion(Expansion.Tbc);
        using var skel =
            WoWLib.Formats.M2.Skeleton.ForVersion(Expansion.Shadowlands);
        foreach (var entity in new object[] { wmo, m2, wdt, wdl, skel })
        {
            var read = entity.GetType().GetMethod(
                "Read", new[] { typeof(WoWLib.Fs.FileSystem), typeof(FileKey) });
            Assert.NotNull(read);
        }

        using var bare = new WoWLib.Formats.ADT.ADT();
        Assert.Throws<System.InvalidOperationException>(
            () => bare.Read(null!, null!, default));
    }

    [Fact]
    public void SequenceWrappersSupportForeach()
    {
        // Duck-typed enumerators: foreach compiles on every sequence
        // wrapper — scalar vectors sum, class vectors yield live views, and
        // an empty vector yields nothing. Vector<T> is the GENERIC container
        // (one type for every element; the registry resolves the native
        // instantiation).
        var numbers = new Vector<ushort>();
        numbers.Add(1);
        numbers.Add(2);
        numbers.Add(4);
        var sum = 0;
        foreach (var n in numbers)
            sum += n;
        Assert.Equal(7, sum);

        using var root = WoWLib.Formats.WMO.Root.WMORoot.Era.Wotlk();
        var count = 0;
        foreach (var material in root.Materials)
            ++count;
        Assert.Equal(0, count);
    }

    [Fact]
    public void StandaloneFamiliesStillGetPerEraStatics()
    {
        // M2SkinProfile welds standalone concretes (no family base), so it has
        // no ForVersion — but the typed per-era statics must exist.
        using var vanilla = WoWLib.Formats.M2.Skin.M2SkinProfile.Era.Vanilla();
        using var wotlk = WoWLib.Formats.M2.Skin.M2SkinProfile.Era.Wotlk();
        Assert.IsType<WoWLib.Formats.M2.Skin.M2SkinProfileVanilla>(vanilla);
        Assert.IsType<WoWLib.Formats.M2.Skin.M2SkinProfileTbcToWotlk>(wotlk);
    }

    [Theory]
    // The Classic axis: Expansion cannot name these clients (Cataclysm Classic
    // is not Cataclysm), so ForVersion takes a full ClientVersion and places it
    // by build number on the retail engine timeline.
    [InlineData(2, 5, 4, 44833u, typeof(WoWLib.Formats.WMO.WMOShadowlands))]
    [InlineData(4, 4, 2, 60895u, typeof(WoWLib.Formats.WMO.WMOTheWarWithin))]
    [InlineData(1, 15, 9, 69109u, typeof(WoWLib.Formats.WMO.WMOTheWarWithin))]
    public void ForVersionAcceptsAClassicClientVersion(
        int major, int minor, int patch, uint build, System.Type expected)
    {
        using var version = new ClientVersion((ushort)major, (ushort)minor,
                                              (ushort)patch, build,
                                              ClientFlavor.Classic);
        using var wmo = WoWLib.Formats.WMO.WMO.ForVersion(version);
        Assert.Equal(expected, wmo.GetType());
    }

    [Fact]
    public void ForVersionOnARetailVersionMatchesTheExpansionOverload()
    {
        using var byVersion = WoWLib.Formats.ADT.ADT.ForVersion(Versions.Global.Wotlk);
        using var byEra = WoWLib.Formats.ADT.ADT.ForVersion(Expansion.Wotlk);
        Assert.Equal(byEra.GetType(), byVersion.GetType());
    }

    [Fact]
    public void SameVersionNumberDifferentBuildDifferentEngine()
    {
        // Cataclysm Classic 4.4.0 shipped on Dragonflight and, months later,
        // on The War Within. Only the build tells them apart.
        using var early = new ClientVersion(4, 4, 0, 54481, ClientFlavor.Classic);
        using var late = new ClientVersion(4, 4, 0, 57244, ClientFlavor.Classic);
        using var earlyWmo = WoWLib.Formats.WMO.WMO.ForVersion(early);
        using var lateWmo = WoWLib.Formats.WMO.WMO.ForVersion(late);
        Assert.Equal(typeof(WoWLib.Formats.WMO.WMODragonflight), earlyWmo.GetType());
        Assert.Equal(typeof(WoWLib.Formats.WMO.WMOTheWarWithin), lateWmo.GetType());
    }
}
