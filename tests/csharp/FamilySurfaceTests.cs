// The rod-synthesized family surface (welder-csharp options::family_surface):
// a ForVersion(...) result carries DATA, not just verbs — the member
// intersection every era binds identically lives on the family base as
// dispatch members, welded members surface as their own family base, and
// welded sequences surface as FamilyVector<Base> live views. The C# twin of
// Python's AnyWMO intersection semantics; client-free like the rest of this
// suite.

using wowlib;
using Formats = wowlib.Formats;
using Xunit;

namespace Wowlib.Tests;

public class FamilySurfaceTests
{
    [Fact]
    public void BaseTypedDataAccessSpansTheWholeTree()
    {
        using var wmo = Formats.Wmo.WMO.ForVersion(Expansion.Wotlk);

        // Groups is a FamilyVector<WMOGroup> live view; mutation goes through
        // the concrete (the documented pattern-matching story), and the
        // base-typed view sees it immediately.
        Assert.Equal(0, wmo.Groups.Count);
        var concrete = Assert.IsType<Formats.Wmo.WMOVanillaToWotlk>(wmo);
        concrete.Groups.Add(new Formats.Wmo.Group.WMOGroupVanillaToWotlk());
        Assert.Equal(1, wmo.Groups.Count);

        // The element is the group family base; ITS hoisted members nest all
        // the way down: base group -> base body -> the shared index vector.
        Formats.Wmo.Group.WMOGroup group = wmo.Groups[0];
        Formats.Wmo.Group.WMOGroupBody body = group.Body;
        body.Indices.Add(1);
        body.Indices.Add(2);
        body.Indices.Add(3);

        var triangles = 0;
        foreach (Formats.Wmo.Group.WMOGroup g in wmo.Groups)
            triangles += g.Body.Indices.Count / 3;
        Assert.Equal(1, triangles);
    }

    [Fact]
    public void WeldedMembersHoistAsTheirOwnFamilyBase()
    {
        using var wmo = Formats.Wmo.WMO.ForVersion(Expansion.Wotlk);
        // WMO.Root is WMORootVanillaToWod on this era; through the base it is
        // the WMORoot family base — still fully usable, itself dispatched.
        Formats.Wmo.Root.WMORoot root = wmo.Root;
        Assert.NotNull(root);
    }

    [Fact]
    public void WrongEraAssignmentThrowsInvalidCast()
    {
        using var wmo = Formats.Wmo.WMO.ForVersion(Expansion.Wotlk);
        using var slRoot = new Formats.Wmo.Root.WMORootShadowlandsToDragonflight();
        Assert.Throws<System.InvalidCastException>(() => wmo.Root = slRoot);
    }

    [Fact]
    public void MethodsDispatchOnTheBase()
    {
        using var wmo = Formats.Wmo.WMO.ForVersion(Expansion.Bfa);
        // Validate is welded on the concretes with one spelling — the family
        // surface hoists it, so the base runs it without a downcast.
        using var report = wmo.Validate();
        Assert.NotNull(report);
    }

    [Fact]
    public void BareBaseInstanceThrowsFromTheDispatchDefaultArm()
    {
        using var bare = new Formats.Wmo.WMO();
        Assert.Throws<System.InvalidOperationException>(() =>
        {
            _ = bare.Groups;
        });
    }
}
