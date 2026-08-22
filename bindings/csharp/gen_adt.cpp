/** @file
    @brief The ADT version-matrix contribution to the C# generator
    (see gen_contributors.hpp for the multi-TU story). The X-macro tables in
    adt_ranges.hpp are the single source of truth for which per-range
    aliases exist; this TU welds exactly those, explicitly, into the shared
    document — each family into the same nested C# namespace the single-TU
    walk gave it. */

#include <wowlib/wowlib.hpp>

#include "instantiations/adt_ranges.hpp"

#include <welder/naming.hpp>
#include <welder/rods/csharp/naming.hpp>
#include <welder/rods/csharp/rod.hpp>

#include "gen_contributors.hpp"

namespace wowlib_cs
{
  void contribute_adt(::welder::rods::csharp::document& doc)
  {
    namespace wcs = ::welder::rods::csharp;
    using W = ::welder::welder<wcs::rod, wcs::dotnet>;
    auto m0 = wcs::rod::at(doc, "Formats.ADT");

    #define X(S, v) W::weld_type<^^::wowlib::formats::adt::ADT##S>(m0, "ADT" #S);
    WOWLIB_ADT_RANGES_ASSEMBLY(X)
#undef X

    #define X(S, v) W::weld_type<^^::wowlib::formats::adt::MapChunk##S>(m0, "MapChunk" #S);
    WOWLIB_ADT_RANGES_MAPCHUNK(X)
#undef X
  }
}
