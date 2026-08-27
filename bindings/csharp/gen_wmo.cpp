/** @file
    @brief The WMO version-matrix contribution to the C# generator
    (see gen_contributors.hpp for the multi-TU story). The x-macro tables in
    wmo_ranges.hpp are the single source of truth for which per-range
    aliases exist; this TU welds exactly those, explicitly, into the shared
    document — each family into the same nested C# namespace the single-TU
    walk gave it. */

#include <wowlib/wowlib.hpp>

#include "instantiations/wmo_ranges.hpp"

#include <welder/naming.hpp>
#include <welder/rods/csharp/naming.hpp>
#include <welder/rods/csharp/rod.hpp>

#include "gen_contributors.hpp"

namespace wowlib_cs
{
  void contributeWmo(::welder::rods::csharp::document& doc)
  {
    namespace wcs = ::welder::rods::csharp;
    using W = ::welder::welder<wcs::rod, wcs::dotnet>;
    auto m0 = wcs::rod::at(doc, "Formats.WMO.Root");
    auto m1 = wcs::rod::at(doc, "Formats.WMO.Group");
    auto m2 = wcs::rod::at(doc, "Formats.WMO.Group.Chunks");
    auto m3 = wcs::rod::at(doc, "Formats.WMO");

    #define x(S, v) W::weld_type<^^::wowlib::formats::wmo::root::WMORoot##S>(m0, "WMORoot" #S);
    WOWLIB_WMO_RANGES_ROOT(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wmo::group::WMOGroupBody##S>(m1, "WMOGroupBody" #S); W::weld_type<^^::wowlib::formats::wmo::group::WMOGroup##S>(m1, "WMOGroup" #S);
    WOWLIB_WMO_RANGES_GROUP(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wmo::group::chunks::WMOGroupHeader##S>(m2, "WMOGroupHeader" #S);
    WOWLIB_WMO_RANGES_GROUP_HEADER(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wmo::group::chunks::WMOBatch##S>(m2, "WMOBatch" #S);
    WOWLIB_WMO_RANGES_BATCH(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wmo::WMO##S>(m3, "WMO" #S);
    WOWLIB_WMO_RANGES_ASSEMBLY(x)
#undef x
  }
}
