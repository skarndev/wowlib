/** @file
    @brief The WMO version-matrix welds for the Python module
    (one parallel TU per format — the single module-walk TU was the build's
    serial tail; see gen_wmo.cpp for the C# twin of this split).

    weld_type per DISTINCT concrete, never weld_namespace: a namespace weld
    instantiates one bind_namespace specialization whose membersOf bakes
    from an arbitrary TU (the db shards learned this) — explicit types keyed
    on the concrete never merge. The x-macro tables in wmo_ranges.hpp are
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

#include "instantiations/wmo.hpp"

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

  void registerWmoWelds(::nanobind::module_& root)
  {
    using W = ::welder::welder<::welder::rods::nanobind::rod<>,
                               wowlib_py::WowlibPythonNaming>;
    ::nanobind::module_ m0 = submodule(submodule(submodule(root, "formats"), "wmo"), "root");
    ::nanobind::module_ m1 = submodule(submodule(submodule(root, "formats"), "wmo"), "group");
    ::nanobind::module_ m2 = submodule(submodule(submodule(submodule(root, "formats"), "wmo"), "group"), "chunks");
    ::nanobind::module_ m3 = submodule(submodule(root, "formats"), "wmo");

#define x(S, v) W::weld_type<::wowlib::formats::wmo::root::WMORoot##S>(m0, "WMORoot" #S);
    WOWLIB_WMO_RANGES_ROOT(x)
#undef x

#define x(S, v) W::weld_type<::wowlib::formats::wmo::group::WMOGroupBody##S>(m1, "WMOGroupBody" #S); W::weld_type<::wowlib::formats::wmo::group::WMOGroup##S>(m1, "WMOGroup" #S);
    WOWLIB_WMO_RANGES_GROUP(x)
#undef x

#define x(S, v) W::weld_type<::wowlib::formats::wmo::group::chunks::WMOGroupHeader##S>(m2, "WMOGroupHeader" #S);
    WOWLIB_WMO_RANGES_GROUP_HEADER(x)
#undef x

#define x(S, v) W::weld_type<::wowlib::formats::wmo::group::chunks::WMOBatch##S>(m2, "WMOBatch" #S);
    WOWLIB_WMO_RANGES_BATCH(x)
#undef x

#define x(S, v) W::weld_type<::wowlib::formats::wmo::WMO##S>(m3, "WMO" #S);
    WOWLIB_WMO_RANGES_ASSEMBLY(x)
#undef x
  }
}
