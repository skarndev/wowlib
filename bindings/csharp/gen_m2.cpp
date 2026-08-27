/** @file
    @brief The M2 version-matrix contribution to the C# generator
    (see gen_contributors.hpp for the multi-TU story). The x-macro tables in
    m2_ranges.hpp are the single source of truth for which per-range
    aliases exist; this TU welds exactly those, explicitly, into the shared
    document — each family into the same nested C# namespace the single-TU
    walk gave it. */

#include <wowlib/wowlib.hpp>

#include "instantiations/m2_ranges.hpp"

#include <welder/naming.hpp>
#include <welder/rods/csharp/naming.hpp>
#include <welder/rods/csharp/rod.hpp>

#include "gen_contributors.hpp"

namespace wowlib_cs
{
  void contribute_m2(::welder::rods::csharp::document& doc)
  {
    namespace wcs = ::welder::rods::csharp;
    using W = ::welder::welder<wcs::rod, wcs::dotnet>;
    auto m0 = wcs::rod::at(doc, "Formats.M2.Root.Record");

    // The M2Track value families' BASES (alias-welded — templates are
    // invisible to the namespace walk), named as the families ARE:
    namespace rec = ::wowlib::formats::m2::root::record;
    W::weld_type<^^rec::M2TrackC3VectorFam>(m0, "M2TrackC3Vector");
    W::weld_type<^^rec::M2TrackC4QuaternionFam>(m0, "M2TrackC4Quaternion");
    W::weld_type<^^rec::M2TrackCompQuatFam>(m0, "M2TrackCompQuat");
    W::weld_type<^^rec::M2TrackFloatFam>(m0, "M2TrackFloat");
    W::weld_type<^^rec::M2TrackFixed16Fam>(m0, "M2TrackFixed16");
    W::weld_type<^^rec::M2TrackUInt8Fam>(m0, "M2TrackUInt8");
    W::weld_type<^^rec::M2TrackUInt16Fam>(m0, "M2TrackUInt16");
    W::weld_type<^^rec::M2TrackSplineC3VectorFam>(m0, "M2TrackSplineC3Vector");
    W::weld_type<^^rec::M2TrackSplineFloatFam>(m0, "M2TrackSplineFloat");

    // The version-INDEPENDENT payload aliases the track types reference
    // (declared standalone in m2_ranges.hpp, outside the x-macro tables —
    // the old single-TU walk welded them through its alias branch).
    namespace rec = ::wowlib::formats::m2::root::record;
    W::weld_type<^^rec::M2SplineKeyC3Vector>(m0, "M2SplineKeyC3Vector");
    W::weld_type<^^rec::M2SplineKeyFloat>(m0, "M2SplineKeyFloat");
    W::weld_type<^^rec::FBlockC3Vector>(m0, "FBlockC3Vector");
    W::weld_type<^^rec::FBlockC2Vector>(m0, "FBlockC2Vector");
    W::weld_type<^^rec::FBlockFixed16>(m0, "FBlockFixed16");
    W::weld_type<^^rec::FBlockUInt16>(m0, "FBlockUInt16");
    W::weld_type<^^rec::M2PartTrackFixed16>(m0, "M2PartTrackFixed16");
    auto m1 = wcs::rod::at(doc, "Formats.M2.Root");
    auto m2 = wcs::rod::at(doc, "Formats.M2.Chunked");
    auto m3 = wcs::rod::at(doc, "Formats.M2.Skin");
    auto m4 = wcs::rod::at(doc, "Formats.M2");

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackC3Vector##S>(m0, "M2TrackC3Vector" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackC4Quaternion##S>(m0, "M2TrackC4Quaternion" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackCompQuat##S>(m0, "M2TrackCompQuat" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackFloat##S>(m0, "M2TrackFloat" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackFixed16##S>(m0, "M2TrackFixed16" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackUInt8##S>(m0, "M2TrackUInt8" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackUInt16##S>(m0, "M2TrackUInt16" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackSplineC3Vector##S>(m0, "M2TrackSplineC3Vector" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TrackSplineFloat##S>(m0, "M2TrackSplineFloat" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2EventTrack##S>(m0, "M2EventTrack" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2Color##S>(m0, "M2Color" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TextureWeight##S>(m0, "M2TextureWeight" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TextureFlipbook##S>(m0, "M2TextureFlipbook" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2TextureTransform##S>(m0, "M2TextureTransform" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2Attachment##S>(m0, "M2Attachment" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2Event##S>(m0, "M2Event" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2Light##S>(m0, "M2Light" #S); W::weld_type<^^::wowlib::formats::m2::root::record::M2Ribbon##S>(m0, "M2Ribbon" #S);
    WOWLIB_M2_RANGES_TRACKS(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::root::record::M2Sequence##S>(m0, "M2Sequence" #S);
    WOWLIB_M2_RANGES_SEQUENCE(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::root::record::M2CompBone##S>(m0, "M2CompBone" #S);
    WOWLIB_M2_RANGES_BONE(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::root::record::M2Camera##S>(m0, "M2Camera" #S);
    WOWLIB_M2_RANGES_CAMERA(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::root::record::M2Particle##S>(m0, "M2Particle" #S);
    WOWLIB_M2_RANGES_PARTICLE(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::root::M2Root##S>(m1, "M2Root" #S);
    WOWLIB_M2_RANGES_DATA(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::chunked::M2ChunkedFile##S>(m2, "M2ChunkedFile" #S);
    WOWLIB_M2_RANGES_FILE(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::skin::M2SkinSection##S>(m3, "M2SkinSection" #S);
    WOWLIB_M2_RANGES_SKIN_SECTION(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::skin::M2SkinProfile##S>(m3, "M2SkinProfile" #S);
    WOWLIB_M2_RANGES_SKIN_PROFILE(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::skin::Skin##S>(m3, "Skin" #S);
    WOWLIB_M2_RANGES_SKIN(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::SkelHeader##S>(m4, "SkelHeader" #S); W::weld_type<^^::wowlib::formats::m2::SkelSequences##S>(m4, "SkelSequences" #S); W::weld_type<^^::wowlib::formats::m2::SkelBones##S>(m4, "SkelBones" #S); W::weld_type<^^::wowlib::formats::m2::SkelAttachments##S>(m4, "SkelAttachments" #S); W::weld_type<^^::wowlib::formats::m2::Skeleton##S>(m4, "Skeleton" #S);
    WOWLIB_M2_RANGES_CHUNK_PAYLOADS(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::m2::M2##S>(m4, "M2" #S);
    WOWLIB_M2_RANGES_ASSEMBLY(x)
#undef x
  }
}
