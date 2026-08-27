/** @file
    @brief The ADT version-matrix welds for the Python module
    (one parallel TU per format — the single module-walk TU was the build's
    serial tail; see gen_adt.cpp for the C# twin of this split).

    weld_type per DISTINCT concrete, never weld_namespace: a namespace weld
    instantiates one bind_namespace specialization whose membersOf bakes
    from an arbitrary TU (the db shards learned this) — explicit types keyed
    on the concrete never merge. The x-macro tables in adt_ranges.hpp are
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

#include "instantiations/adt.hpp"

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

  void registerAdtWelds(::nanobind::module_& root)
  {
    using W = ::welder::welder<::welder::rods::nanobind::rod<>,
                               wowlib_py::WowlibPythonNaming>;
    ::nanobind::module_ m0 = submodule(submodule(root, "formats"), "adt");

#define x(S, v) W::weld_type<::wowlib::formats::adt::ADT##S>(m0, "ADT" #S);
    WOWLIB_ADT_RANGES_ASSEMBLY(x)
#undef x

#define x(S, v) W::weld_type<::wowlib::formats::adt::MapChunk##S>(m0, "MapChunk" #S);
    WOWLIB_ADT_RANGES_MAPCHUNK(x)
#undef x
  }
}
