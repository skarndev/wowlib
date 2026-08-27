#pragma once

/** @file
    @brief Per-RANGE welded alias tables for the WDL template surface.

    One row per REAL content permutation — x(Suffix, version) with the
    range's canonical grid version — driving the welded aliases here plus the
    instantiation matrix in wdl_matrix.inl, consteval-checked against
    WdlPivots. Lives in the bindings, not the library (see
    wdt_ranges.hpp). */

#include <wowlib/formats/wdl/wdl.hpp>

#define WOWLIB_WDL_RANGES(x)                                                                       \
  x(Vanilla, Vanilla)                                                                              \
  x(TbcToWod, Tbc)                                                                                 \
  x(Legion, Legion)                                                                                \
  x(Bfa, Bfa)                                                                                      \
  x(ShadowlandsToDragonflight, Shadowlands)                                                        \
  x(TheWarWithin, Tww)

namespace wowlib::formats::wdl
{
#define WOWLIB_WDL_ALIAS(Suffix, version_) using WDL##Suffix = WDL<versions::version_>;
  WOWLIB_WDL_RANGES(WOWLIB_WDL_ALIAS)
#undef WOWLIB_WDL_ALIAS

  namespace detail
  {
#define WOWLIB_WDL_RANGE_ROW(Suffix, version_)                                                     \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array WdlRows{WOWLIB_WDL_RANGES(WOWLIB_WDL_RANGE_ROW)};
    static_assert(rangesValid(WdlRows, WdlPivots, WdlVersions),
                  "WOWLIB_WDL_RANGES drifted from wdl_pivots");
#undef WOWLIB_WDL_RANGE_ROW
  }
}
