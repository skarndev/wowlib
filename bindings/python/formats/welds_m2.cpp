/** @file
    @brief The M2 version-matrix welds for the Python module
    (one parallel TU per format — the single module-walk TU was the build's
    serial tail; see gen_m2.cpp for the C# twin of this split).

    weld_type per DISTINCT concrete, never weld_namespace: a namespace weld
    instantiates one bind_namespace specialization whose membersOf bakes
    from an arbitrary TU (the db shards learned this) — explicit types keyed
    on the concrete never merge. The x-macro tables in m2_ranges.hpp are
    the single source of truth for which aliases exist. */

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <wowlib/wowlib.hpp>

#include "instantiations/m2.hpp"

#include "result_casters.hpp"
#include "formats/repeated_caster.hpp"
#include "naming.hpp"

#include <welder/naming.hpp>
#include <welder/rods/python/nanobind/rod.hpp>

// Same opaque-container declarations as every welding TU (a TU binding a
// container the module TU marked opaque MUST see the same NB_MAKE_OPAQUE).
#include "wowlib.opaque.hpp"

#include "formats/welds.hpp"

namespace wowlib_py::formats
{
  namespace
  {
    /** The walk-created submodule at @a name under @a parent (created here
        when this format's namespace level held nothing weld-walkable). */
    ::nanobind::module_ submodule(::nanobind::module_ parent, const char* name)
    {
      if (::nanobind::hasattr(parent, name))
        return ::nanobind::borrow<::nanobind::module_>(parent.attr(name));
      return parent.def_submodule(name);
    }
  }

  void registerM2Welds(::nanobind::module_& root)
  {
    using W = ::welder::welder<::welder::rods::nanobind::rod<>,
                               wowlib_py::WowlibPythonNaming>;
    ::nanobind::module_ m0 = submodule(submodule(submodule(submodule(root, "formats"), "m2"), "root"), "record");
    ::nanobind::module_ m1 = submodule(submodule(submodule(root, "formats"), "m2"), "root");
    ::nanobind::module_ m2 = submodule(submodule(submodule(root, "formats"), "m2"), "chunked");
    ::nanobind::module_ m3 = submodule(submodule(submodule(root, "formats"), "m2"), "skin");
    ::nanobind::module_ m4 = submodule(submodule(root, "formats"), "m2");

    // The version-INDEPENDENT payload aliases (declared standalone in the
    // ranges header, outside the x-macro tables).
    namespace rec = ::wowlib::formats::m2::root::record;

    // The M2Track VALUE families' version-agnostic BASES: class-template
    // instantiations, invisible to the module walk that registers every
    // plain *Base — weld them here, BEFORE the per-era concretes that
    // derive them (nanobind requires a base registered first; missing one
    // is a critical error at import, which is how their absence surfaced).
    W::weld_type<rec::M2TrackC3VectorFam>(m0, "M2TrackC3Vector");
    W::weld_type<rec::M2TrackC4QuaternionFam>(m0, "M2TrackC4Quaternion");
    W::weld_type<rec::M2TrackCompQuatFam>(m0, "M2TrackCompQuat");
    W::weld_type<rec::M2TrackFloatFam>(m0, "M2TrackFloat");
    W::weld_type<rec::M2TrackFixed16Fam>(m0, "M2TrackFixed16");
    W::weld_type<rec::M2TrackUInt8Fam>(m0, "M2TrackUInt8");
    W::weld_type<rec::M2TrackUInt16Fam>(m0, "M2TrackUInt16");
    W::weld_type<rec::M2TrackSplineC3VectorFam>(m0, "M2TrackSplineC3Vector");
    W::weld_type<rec::M2TrackSplineFloatFam>(m0, "M2TrackSplineFloat");
    W::weld_type<rec::M2SplineKeyC3Vector>(m0, "M2SplineKeyC3Vector");
    W::weld_type<rec::M2SplineKeyFloat>(m0, "M2SplineKeyFloat");
    W::weld_type<rec::FBlockC3Vector>(m0, "FBlockC3Vector");
    W::weld_type<rec::FBlockC2Vector>(m0, "FBlockC2Vector");
    W::weld_type<rec::FBlockFixed16>(m0, "FBlockFixed16");
    W::weld_type<rec::FBlockUInt16>(m0, "FBlockUInt16");
    W::weld_type<rec::M2PartTrackFixed16>(m0, "M2PartTrackFixed16");

#define X(S, v) W::weld_type<::wowlib::formats::m2::root::record::M2TrackC3Vector##S>(m0, "M2TrackC3Vector" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackC4Quaternion##S>(m0, "M2TrackC4Quaternion" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackCompQuat##S>(m0, "M2TrackCompQuat" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackFloat##S>(m0, "M2TrackFloat" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackFixed16##S>(m0, "M2TrackFixed16" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackUInt8##S>(m0, "M2TrackUInt8" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackUInt16##S>(m0, "M2TrackUInt16" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackSplineC3Vector##S>(m0, "M2TrackSplineC3Vector" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TrackSplineFloat##S>(m0, "M2TrackSplineFloat" #S); W::weld_type<::wowlib::formats::m2::root::record::M2EventTrack##S>(m0, "M2EventTrack" #S); W::weld_type<::wowlib::formats::m2::root::record::M2Color##S>(m0, "M2Color" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TextureWeight##S>(m0, "M2TextureWeight" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TextureFlipbook##S>(m0, "M2TextureFlipbook" #S); W::weld_type<::wowlib::formats::m2::root::record::M2TextureTransform##S>(m0, "M2TextureTransform" #S); W::weld_type<::wowlib::formats::m2::root::record::M2Attachment##S>(m0, "M2Attachment" #S); W::weld_type<::wowlib::formats::m2::root::record::M2Event##S>(m0, "M2Event" #S); W::weld_type<::wowlib::formats::m2::root::record::M2Light##S>(m0, "M2Light" #S); W::weld_type<::wowlib::formats::m2::root::record::M2Ribbon##S>(m0, "M2Ribbon" #S);
    WOWLIB_M2_RANGES_TRACKS(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::root::record::M2Sequence##S>(m0, "M2Sequence" #S);
    WOWLIB_M2_RANGES_SEQUENCE(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::root::record::M2CompBone##S>(m0, "M2CompBone" #S);
    WOWLIB_M2_RANGES_BONE(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::root::record::M2Camera##S>(m0, "M2Camera" #S);
    WOWLIB_M2_RANGES_CAMERA(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::root::record::M2Particle##S>(m0, "M2Particle" #S);
    WOWLIB_M2_RANGES_PARTICLE(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::root::M2Root##S>(m1, "M2Root" #S);
    WOWLIB_M2_RANGES_DATA(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::chunked::M2ChunkedFile##S>(m2, "M2ChunkedFile" #S);
    WOWLIB_M2_RANGES_FILE(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::skin::M2SkinSection##S>(m3, "M2SkinSection" #S);
    WOWLIB_M2_RANGES_SKIN_SECTION(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::skin::M2SkinProfile##S>(m3, "M2SkinProfile" #S);
    WOWLIB_M2_RANGES_SKIN_PROFILE(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::skin::Skin##S>(m3, "Skin" #S);
    WOWLIB_M2_RANGES_SKIN(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::SkelHeader##S>(m4, "SkelHeader" #S); W::weld_type<::wowlib::formats::m2::SkelSequences##S>(m4, "SkelSequences" #S); W::weld_type<::wowlib::formats::m2::SkelBones##S>(m4, "SkelBones" #S); W::weld_type<::wowlib::formats::m2::SkelAttachments##S>(m4, "SkelAttachments" #S); W::weld_type<::wowlib::formats::m2::Skeleton##S>(m4, "Skeleton" #S);
    WOWLIB_M2_RANGES_CHUNK_PAYLOADS(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::m2::M2##S>(m4, "M2" #S);
    WOWLIB_M2_RANGES_ASSEMBLY(X)
#undef X
  }
}
