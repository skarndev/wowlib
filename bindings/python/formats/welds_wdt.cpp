/** @file
    @brief The WDT version-matrix welds for the Python module
    (one parallel TU per format — the single module-walk TU was the build's
    serial tail; see gen_wdt.cpp for the C# twin of this split).

    weld_type per DISTINCT concrete, never weld_namespace: a namespace weld
    instantiates one bind_namespace specialization whose membersOf bakes
    from an arbitrary TU (the db shards learned this) — explicit types keyed
    on the concrete never merge. The x-macro tables in wdt_ranges.hpp are
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

#include "instantiations/wdt.hpp"

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

  void registerWdtWelds(::nanobind::module_& root)
  {
    using W = ::welder::welder<::welder::rods::nanobind::rod<>,
                               wowlib_py::WowlibPythonNaming>;
    ::nanobind::module_ m0 = submodule(submodule(submodule(root, "formats"), "wdt"), "root");
    ::nanobind::module_ m1 = submodule(submodule(submodule(submodule(root, "formats"), "wdt"), "root"), "chunks");
    ::nanobind::module_ m2 = submodule(submodule(submodule(root, "formats"), "wdt"), "occlusion");
    ::nanobind::module_ m3 = submodule(submodule(submodule(root, "formats"), "wdt"), "lights");
    ::nanobind::module_ m4 = submodule(submodule(submodule(root, "formats"), "wdt"), "fogs");
    ::nanobind::module_ m5 = submodule(submodule(submodule(root, "formats"), "wdt"), "mpv");
    ::nanobind::module_ m6 = submodule(submodule(root, "formats"), "wdt");

#define X(S, v) W::weld_type<::wowlib::formats::wdt::root::WDTRoot##S>(m0, "WDTRoot" #S);
    WOWLIB_WDT_RANGES_ROOT(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::wdt::root::chunks::WDTHeader##S>(m1, "WDTHeader" #S);
    WOWLIB_WDT_RANGES_HEADER(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::wdt::occlusion::WDTOcclusion##S>(m2, "WDTOcclusion" #S);
    WOWLIB_WDT_RANGES_OCCLUSION(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::wdt::lights::WDTLights##S>(m3, "WDTLights" #S);
    WOWLIB_WDT_RANGES_LIGHTS(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::wdt::fogs::WDTFogs##S>(m4, "WDTFogs" #S);
    WOWLIB_WDT_RANGES_FOGS(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::wdt::mpv::WDTParticulates##S>(m5, "WDTParticulates" #S);
    WOWLIB_WDT_RANGES_MPV(X)
#undef X

#define X(S, v) W::weld_type<::wowlib::formats::wdt::WDT##S>(m6, "WDT" #S);
    WOWLIB_WDT_RANGES_ASSEMBLY(X)
#undef X
  }
}
