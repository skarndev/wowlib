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
    public void RecordCountsReadThroughTheBase()
    {
        // The user-reported flaw: reading root.Sequences.Count needed a
        // six-arm per-era switch, because the M2 record families welded
        // standalone. They have bases now, so the collection members hoist
        // as FamilyVector<RecordBase> — Count and element views included.
        using var root = Formats.M2.Root.M2Root.ForVersion(Expansion.Wotlk);
        Assert.Equal(0, root.Sequences.Count);
        Assert.Equal(0, root.Bones.Count);
        Assert.Equal(0, root.ParticleEmitters.Count);
        Assert.Equal(0, root.Attachments.Count);

        // Populated through the concrete, counted and read through the base.
        var concrete =
            Assert.IsAssignableFrom<Formats.M2.Root.M2RootWotlk>(root);
        concrete.Sequences.Add(
            new Formats.M2.Root.Record.M2SequenceWotlkToMop());
        Assert.Equal(1, root.Sequences.Count);
        Formats.M2.Root.Record.M2Sequence seq = root.Sequences[0];
        Assert.NotNull(seq);
        // The record family base has its own ForVersion now, like every base.
        using var anySeq = Formats.M2.Root.Record.M2Sequence.ForVersion(
            Expansion.Wotlk);
        Assert.IsAssignableFrom<Formats.M2.Root.Record.M2Sequence>(anySeq);
    }

    [Fact]
    public void TrackTimelinesAreVersionAgnostic()
    {
        // The canonical key surface: the two track layouts (global timeline
        // + ranges pre-WotLK vs per-sequence arrays WotLK+) answer the SAME
        // hoisted accessors, so version-agnostic code never branches.
        using var pre = Formats.M2.Root.Record.M2TrackC3Vector.ForVersion(
            Expansion.Tbc);
        var preC = Assert.IsAssignableFrom<
            Formats.M2.Root.Record.M2TrackC3VectorVanillaToTbc>(pre);
        preC.Timestamps.Add(0); preC.Timestamps.Add(10);
        preC.Timestamps.Add(20); preC.Timestamps.Add(30);
        for (int i = 0; i < 4; i++)
            using (var v = new Formats.Common.C3Vector())
                preC.Values.Add(v);
        using (var r0 = new Formats.M2.Root.Record.M2Range())
        using (var r1 = new Formats.M2.Root.Record.M2Range())
        {
            r0.Minimum = 0; r0.Maximum = 1;
            r1.Minimum = 2; r1.Maximum = 3;
            preC.InterpolationRanges.Add(r0);
            preC.InterpolationRanges.Add(r1);
        }

        using var post = Formats.M2.Root.Record.M2TrackC3Vector.ForVersion(
            Expansion.Wotlk);
        var postC = Assert.IsAssignableFrom<
            Formats.M2.Root.Record.M2TrackC3VectorWotlkPlus>(post);
        postC.Timestamps.Add(new uint[] { 20, 30 });

        // One loop over BASE-typed tracks spanning both eras:
        foreach (var track in new[] { pre, post })
        {
            Assert.True(track.TimelineCount() >= 1);
            var last = track.TimelineCount() - 1;
            Assert.Equal(2UL, track.KeyCount(last));
            var times = track.TimelineTimestamps(last);
            Assert.Equal(new uint[] { 20, 30 }, times);
        }
        // Out-of-range errors uniformly (the Result error channel).
        Assert.ThrowsAny<System.Exception>(() => pre.KeyCount(99));

        // IAnimTimeline: ONE interface over every track family and era —
        // the timing trio dispatches through the family surface.
        using var evt = Formats.M2.Root.Record.M2EventTrack.ForVersion(
            Expansion.Wotlk);
        var timelines = new Formats.M2.Root.Record.IAnimTimeline[] { pre, post, evt };
        foreach (var tl in timelines)
            for (ulong s = 0; s < tl.TimelineCount(); s++)
                Assert.Equal((int)tl.KeyCount(s),
                             tl.TimelineTimestamps(s).Length);
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
        var span = normals.AsDataSpan();   // the generated sugar for AsSpan<McnrEntry.Data>()
        Assert.Equal(4, span.Length);
        span[2].Normal[1] = -7;                       // straight to native
        Assert.Equal(-7, normals[2].Normal[1]);       // seen by the live view
        Assert.Throws<System.ArgumentException>(
            () => normals.AsSpan<ulong>());           // size gate holds
    }

    [Fact]
    public void ScalarSpanIsCompileTimeGated()
    {
        // AsSpan() is a `where T : unmanaged` EXTENSION now: a scalar vector
        // still spans zero-copy, while a record/nested-element call (the old
        // "requires scalar" runtime throw, e.g. chunk.AlphaMaps.AsSpan())
        // no longer compiles at all.
        using var nums = new Vector<uint> { };
        nums.Add(3); nums.Add(5);
        var span = nums.AsSpan();
        span[1] = 7;
        Assert.Equal(7u, nums[1]);
        // The non-generic instance method is gone — nothing for misuse to
        // bind to (AsSpan<TData>() remains, with its runtime size gate).
        Assert.DoesNotContain(
            typeof(Vector<Formats.ADT.Chunks.McnrEntry>).GetMethods(),
            m => m.Name == "AsSpan" && !m.IsGenericMethod);
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
