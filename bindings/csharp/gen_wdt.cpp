/** @file
    @brief The WDT version-matrix contribution to the C# generator
    (see gen_contributors.hpp for the multi-TU story). The x-macro tables in
    wdt_ranges.hpp are the single source of truth for which per-range
    aliases exist; this TU welds exactly those, explicitly, into the shared
    document — each family into the same nested C# namespace the single-TU
    walk gave it. */

#include <wowlib/wowlib.hpp>

#include "instantiations/wdt_ranges.hpp"

#include <welder/naming.hpp>
#include <welder/rods/csharp/naming.hpp>
#include <welder/rods/csharp/rod.hpp>

#include "gen_contributors.hpp"

namespace wowlib_cs
{
  void contribute_wdt(::welder::rods::csharp::document& doc)
  {
    namespace wcs = ::welder::rods::csharp;
    using W = ::welder::welder<wcs::rod, wcs::dotnet>;
    auto m0 = wcs::rod::at(doc, "Formats.WDT.Root");
    auto m1 = wcs::rod::at(doc, "Formats.WDT.Root.Chunks");
    auto m2 = wcs::rod::at(doc, "Formats.WDT.Occlusion");
    auto m3 = wcs::rod::at(doc, "Formats.WDT.Lights");
    auto m4 = wcs::rod::at(doc, "Formats.WDT.Fogs");
    auto m5 = wcs::rod::at(doc, "Formats.WDT.Mpv");
    auto m6 = wcs::rod::at(doc, "Formats.WDT");

    #define x(S, v) W::weld_type<^^::wowlib::formats::wdt::root::WDTRoot##S>(m0, "WDTRoot" #S);
    WOWLIB_WDT_RANGES_ROOT(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wdt::root::chunks::WDTHeader##S>(m1, "WDTHeader" #S);
    WOWLIB_WDT_RANGES_HEADER(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wdt::occlusion::WDTOcclusion##S>(m2, "WDTOcclusion" #S);
    WOWLIB_WDT_RANGES_OCCLUSION(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wdt::lights::WDTLights##S>(m3, "WDTLights" #S);
    WOWLIB_WDT_RANGES_LIGHTS(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wdt::fogs::WDTFogs##S>(m4, "WDTFogs" #S);
    WOWLIB_WDT_RANGES_FOGS(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wdt::mpv::WDTParticulates##S>(m5, "WDTParticulates" #S);
    WOWLIB_WDT_RANGES_MPV(x)
#undef x

    #define x(S, v) W::weld_type<^^::wowlib::formats::wdt::WDT##S>(m6, "WDT" #S);
    WOWLIB_WDT_RANGES_ASSEMBLY(x)
#undef x
  }
}
