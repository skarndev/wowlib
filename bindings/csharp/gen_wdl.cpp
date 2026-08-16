/** @file
    @brief The WDL version-matrix contribution to the C# generator
    (see gen_contributors.hpp for the multi-TU story). The X-macro tables in
    wdl_ranges.hpp are the single source of truth for which per-range
    aliases exist; this TU welds exactly those, explicitly, into the shared
    document — each family into the same nested C# namespace the single-TU
    walk gave it. */

#include <wowlib/wowlib.hpp>

#include "instantiations/wdl_ranges.hpp"

#include <welder/naming.hpp>
#include <welder/rods/csharp/naming.hpp>
#include <welder/rods/csharp/rod.hpp>

#include "gen_contributors.hpp"

namespace wowlib_cs
{
  void contribute_wdl(::welder::rods::csharp::document& doc)
  {
    namespace wcs = ::welder::rods::csharp;
    using W = ::welder::welder<wcs::rod, wcs::dotnet>;
    auto m0 = wcs::rod::at(doc, "Formats.Wdl");

    #define X(S, v) W::weld_type<^^::wowlib::formats::wdl::WDL##S>(m0, "WDL" #S);
    WOWLIB_WDL_RANGES(X)
#undef X
  }
}
