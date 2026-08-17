// The per-era format factories (the C# for_version): the statics merged onto
// the welded family bases by tools/gen_cs_format_facades.py. These lock the
// tester-reported gap — version-agnostic construction must exist and return
// the era's covering range class.

using wowlib;
using Xunit;

namespace Wowlib.Tests;

public class FormatFactoryTests
{
    [Theory]
    // The ADT assembly ranges (bindings/instantiations/adt_ranges.hpp):
    // Vanilla / Tbc / Wotlk / CataToLegion / BfaPlus.
    [InlineData(Expansion.Vanilla, typeof(wowlib.Formats.Adt.ADTVanilla))]
    [InlineData(Expansion.Tbc, typeof(wowlib.Formats.Adt.ADTTbc))]
    [InlineData(Expansion.Wotlk, typeof(wowlib.Formats.Adt.ADTWotlk))]
    [InlineData(Expansion.Cata, typeof(wowlib.Formats.Adt.ADTCataToLegion))]
    [InlineData(Expansion.Mop, typeof(wowlib.Formats.Adt.ADTCataToLegion))]
    [InlineData(Expansion.Wod, typeof(wowlib.Formats.Adt.ADTCataToLegion))]
    [InlineData(Expansion.Legion, typeof(wowlib.Formats.Adt.ADTCataToLegion))]
    [InlineData(Expansion.Bfa, typeof(wowlib.Formats.Adt.ADTBfaPlus))]
    [InlineData(Expansion.Shadowlands, typeof(wowlib.Formats.Adt.ADTBfaPlus))]
    [InlineData(Expansion.Dragonflight, typeof(wowlib.Formats.Adt.ADTBfaPlus))]
    [InlineData(Expansion.TheWarWithin, typeof(wowlib.Formats.Adt.ADTBfaPlus))]
    public void AdtForVersionCoversEveryEraWithTheCoveringRange(
        Expansion era, System.Type expected)
    {
        using var adt = wowlib.Formats.Adt.ADT.ForVersion(era);
        Assert.Equal(expected, adt.GetType());
    }

    [Fact]
    public void PerEraStaticsReturnTheConcreteRangeClass()
    {
        // Compile-time typing is the point: no cast on the concrete.
        using wowlib.Formats.Adt.ADTWotlk adt = wowlib.Formats.Adt.ADT.Wotlk();
        wowlib.Formats.Adt.ADT asBase = adt;
        Assert.Same(adt, asBase);
    }

    [Fact]
    public void EveryFamilyBaseFactoryConstructs()
    {
        using var wmo = wowlib.Formats.Wmo.WMO.ForVersion(Expansion.Wotlk);
        using var m2 = wowlib.Formats.M2.M2.ForVersion(Expansion.Legion);
        using var wdt = wowlib.Formats.Wdt.WDT.ForVersion(Expansion.Shadowlands);
        using var wdl = wowlib.Formats.Wdl.WDL.ForVersion(Expansion.Vanilla);
        using var root = wowlib.Formats.Wmo.Root.WMORoot.ForVersion(Expansion.Tbc);
        Assert.IsAssignableFrom<wowlib.Formats.Wmo.WMO>(wmo);
        Assert.IsAssignableFrom<wowlib.Formats.M2.M2>(m2);
        Assert.IsAssignableFrom<wowlib.Formats.Wdt.WDT>(wdt);
        Assert.IsAssignableFrom<wowlib.Formats.Wdl.WDL>(wdl);
        Assert.IsAssignableFrom<wowlib.Formats.Wmo.Root.WMORoot>(root);
    }

    [Fact]
    public void FamilyBasesCarryTheFsVerbs()
    {
        // Version-agnostic code is the point of ForVersion: the base must be
        // able to Read/Write without a downcast. Method-group assignments
        // compile-lock the base verbs' signatures; a bare base instance (no
        // concrete era) reports itself instead of nulling into a backend.
        using var adt = wowlib.Formats.Adt.ADT.ForVersion(Expansion.Wotlk);
        System.Action<wowlib.Fs.FileSystem, FileKey,
                      wowlib.Formats.Adt.AlphaFormat> adtRead = adt.Read;
        System.Action<wowlib.Fs.FileSystem, FileKey,
                      wowlib.Formats.Adt.AlphaFormat> adtWrite = adt.Write;
        Assert.NotNull(adtRead);
        Assert.NotNull(adtWrite);

        using var wmo = wowlib.Formats.Wmo.WMO.ForVersion(Expansion.Vanilla);
        using var m2 = wowlib.Formats.M2.M2.ForVersion(Expansion.Legion);
        using var wdt = wowlib.Formats.Wdt.WDT.ForVersion(Expansion.Cata);
        using var wdl = wowlib.Formats.Wdl.WDL.ForVersion(Expansion.Tbc);
        using var skel =
            wowlib.Formats.M2.Skeleton.ForVersion(Expansion.Shadowlands);
        foreach (var entity in new object[] { wmo, m2, wdt, wdl, skel })
        {
            var read = entity.GetType().GetMethod(
                "Read", new[] { typeof(wowlib.Fs.FileSystem), typeof(FileKey) });
            Assert.NotNull(read);
        }

        using var bare = new wowlib.Formats.Adt.ADT();
        Assert.Throws<System.InvalidOperationException>(
            () => bare.Read(null!, null!, default));
    }

    [Fact]
    public void SequenceWrappersSupportForeach()
    {
        // Duck-typed enumerators (welder-csharp 66a6bc9): foreach compiles on
        // every sequence wrapper — scalar vectors sum, class vectors yield
        // live views, and an empty vector yields nothing.
        var numbers = new VectorUshort();
        numbers.Add(1);
        numbers.Add(2);
        numbers.Add(4);
        var sum = 0;
        foreach (var n in numbers)
            sum += n;
        Assert.Equal(7, sum);

        using var root = wowlib.Formats.Wmo.Root.WMORoot.Wotlk();
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
        using var vanilla = wowlib.Formats.M2.Skin.M2SkinProfile.Vanilla();
        using var wotlk = wowlib.Formats.M2.Skin.M2SkinProfile.Wotlk();
        Assert.IsType<wowlib.Formats.M2.Skin.M2SkinProfileVanilla>(vanilla);
        Assert.IsType<wowlib.Formats.M2.Skin.M2SkinProfileTbcToWotlk>(wotlk);
    }
}
