// The rod-synthesized family surface (welder-csharp options::family_surface):
// a ForVersion(...) result carries DATA, not just verbs — the member
// intersection every era binds identically lives on the family base as
// dispatch members, welded members surface as their own family base, and
// welded sequences surface as FamilyVector<Base> live views. The C# twin of
// Python's AnyWMO intersection semantics; client-free like the rest of this
// suite.

using WoWLib;
using Formats = WoWLib.Formats;
using Xunit;

namespace WoWLib.Tests;

public class FamilySurfaceTests
{
    [Fact]
    public void BaseTypedDataAccessSpansTheWholeTree()
    {
        using var wmo = Formats.WMO.WMO.ForVersion(Expansion.Wotlk);

        // Groups is a FamilyVector<WMOGroup> live view; mutation goes through
        // the concrete (the documented pattern-matching story), and the
        // base-typed view sees it immediately.
        Assert.Equal(0, wmo.Groups.Count);
        var concrete = Assert.IsType<Formats.WMO.WMOVanillaToWotlk>(wmo);
        concrete.Groups.Add(new Formats.WMO.Group.WMOGroupVanillaToWotlk());
        Assert.Equal(1, wmo.Groups.Count);

        // The element is the group family base; ITS hoisted members nest all
        // the way down: base group -> base body -> the shared index vector.
        Formats.WMO.Group.WMOGroup group = wmo.Groups[0];
        Formats.WMO.Group.WMOGroupBody body = group.Body;
        body.Indices.Add(1);
        body.Indices.Add(2);
        body.Indices.Add(3);

        var triangles = 0;
        foreach (Formats.WMO.Group.WMOGroup g in wmo.Groups)
            triangles += g.Body.Indices.Count / 3;
        Assert.Equal(1, triangles);

        // The family view's indexer writes too, era-checked: same era
        // assigns, wrong era throws — the family-setter contract.
        using var sameEra = new Formats.WMO.Group.WMOGroupVanillaToWotlk();
        wmo.Groups[0] = sameEra;
        using var wrongEra = new Formats.WMO.Group.WMOGroupCata();
        Assert.Throws<System.InvalidCastException>(() => wmo.Groups[0] = wrongEra);
    }

    [Fact]
    public void WeldedMembersHoistAsTheirOwnFamilyBase()
    {
        using var wmo = Formats.WMO.WMO.ForVersion(Expansion.Wotlk);
        // WMO.Root is WMORootVanillaToWod on this era; through the base it is
        // the WMORoot family base — still fully usable, itself dispatched.
        Formats.WMO.Root.WMORoot root = wmo.Root;
        Assert.NotNull(root);
    }

    [Fact]
    public void WrongEraAssignmentThrowsInvalidCast()
    {
        using var wmo = Formats.WMO.WMO.ForVersion(Expansion.Wotlk);
        using var slRoot = new Formats.WMO.Root.WMORootShadowlandsToDragonflight();
        Assert.Throws<System.InvalidCastException>(() => wmo.Root = slRoot);
    }

    [Fact]
    public void MethodsDispatchOnTheBase()
    {
        using var wmo = Formats.WMO.WMO.ForVersion(Expansion.Bfa);
        // Validate is welded on the concretes with one spelling — the family
        // surface hoists it, so the base runs it without a downcast.
        using var report = wmo.Validate();
        Assert.NotNull(report);
    }

    [Fact]
    public void FileEntityRootRunsTheSharedContract()
    {
        // Every file-level entity derives Formats.FileEntity; the multi-level
        // family surface hoists the contract they ALL bind (Validate /
        // EnsureValid), so a heterogeneous collection processes uniformly —
        // WMO family member and unversioned BLP alike.
        using var wmo = Formats.WMO.WMO.ForVersion(Expansion.Wotlk);
        using var blp = new Formats.BLP.BLP();
        var entities = new Formats.FileEntity[] { wmo, blp };
        foreach (var e in entities)
        {
            using var report = e.Validate();
            Assert.NotNull(report);
        }
        // fs Read/Write do NOT leak to the root: ADT's signature differs, so
        // the intersection correctly excludes them.
        Assert.Null(typeof(Formats.FileEntity).GetMethod("Read"));
        Assert.NotNull(typeof(Formats.WMO.WMO).GetMethod("Read"));
        // The copy-constructor idiom replaced Clone().
        Assert.Null(typeof(Formats.BLP.BLP).GetMethod("Clone"));
        using var copy = new Formats.BLP.BLP(blp);
        Assert.NotNull(copy);
    }

    [Fact]
    public unsafe void BlittableMirrorsCoverTheHotRecords()
    {
        // The renderer-profile fix: per-vertex records read in BULK — one
        // interop crossing per buffer via the blittable Data mirror, instead
        // of several per element through live views.
        using var normals = new Vector<Formats.ADT.Chunks.McnrEntry>();
        for (int i = 0; i < 4; i++)
            using (var e = new Formats.ADT.Chunks.McnrEntry())
                normals.Add(e);
        var span = normals.AsSpan<Formats.ADT.Chunks.McnrEntry.Data>();
        Assert.Equal(4, span.Length);
        span[2].Normal[1] = -7;                       // straight to native
        Assert.Equal(-7, normals[2].Normal[1]);       // seen by the live view
        Assert.Throws<System.ArgumentException>(
            () => normals.AsSpan<ulong>());           // size gate holds
    }

    [Fact]
    public void BareBaseInstanceThrowsFromTheDispatchDefaultArm()
    {
        using var bare = new Formats.WMO.WMO();
        Assert.Throws<System.InvalidOperationException>(() =>
        {
            _ = bare.Groups;
        });
    }
}
