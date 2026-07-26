#pragma once

/** @file
    @brief Per-RANGE welded alias tables for the WDT template surface.

    Each family's X-macro lists one row per REAL content permutation —
    X(Suffix, version) with the range's canonical grid version — and drives
    the welded aliases here plus the instantiation matrix in wdt_matrix.inl.
    Every table is consteval-checked against the family's pivots
    (ranges_valid in wdt::detail below). The satellite families run on their
    era-subset grids (wdt_satellite/fogs/mpv_versions). Extending the version
    list means revisiting the pivot lists in formats/wdt/boundaries.hpp; the
    checks then dictate the rows.

    This lives in the bindings, NOT the library: C++ consumers instantiate
    the templates on demand and never need the flat per-range names — only
    welder does, because it welds a class-template instantiation through a
    namespace-scope alias whose identifier is the target-language name. */

#include <wowlib/formats/wdt/wdt.hpp>

#define WOWLIB_WDT_RANGES_ROOT(X)                                                                  \
  X(VanillaToLegion, vanilla)                                                                      \
  X(Bfa, bfa)                                                                                      \
  X(ShadowlandsPlus, shadowlands)

#define WOWLIB_WDT_RANGES_HEADER(X)                                                                \
  X(VanillaToLegion, vanilla)                                                                      \
  X(BfaPlus, bfa)

#define WOWLIB_WDT_RANGES_OCCLUSION(X)                                                             \
  X(WodPlus, wod)

#define WOWLIB_WDT_RANGES_LIGHTS(X)                                                                \
  X(Wod, wod)                                                                                      \
  X(LegionToBfa, legion)                                                                           \
  X(ShadowlandsPlus, shadowlands)

#define WOWLIB_WDT_RANGES_FOGS(X)                                                                  \
  X(Legion, legion)                                                                                \
  X(BfaToDragonflight, bfa)                                                                        \
  X(TheWarWithin, tww)

#define WOWLIB_WDT_RANGES_MPV(X)                                                                   \
  X(BfaPlus, bfa)

#define WOWLIB_WDT_RANGES_ASSEMBLY(X)                                                              \
  X(VanillaToMop, vanilla)                                                                         \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(ShadowlandsToDragonflight, shadowlands)                                                        \
  X(TheWarWithin, tww)

// The bindings surface for the versioned templates: welder welds a
// class-template instantiation through a namespace-scope alias, whose
// identifier is the target-language name. Each family's aliases are declared
// in its own namespace so the per-range classes surface under the matching
// submodule, mirroring the C++ layout.
namespace wowlib::formats::wdt::root
{
#define WOWLIB_WDT_ROOT_ALIAS(Suffix, version_) using WDTRoot##Suffix = WDTRoot<versions::version_>;
  WOWLIB_WDT_RANGES_ROOT(WOWLIB_WDT_ROOT_ALIAS)
#undef WOWLIB_WDT_ROOT_ALIAS
}

namespace wowlib::formats::wdt::root::chunks
{
#define WOWLIB_WDT_HEADER_ALIAS(Suffix, version_)                                                  \
  using WDTHeader##Suffix = SMMapHeader<versions::version_>;
  WOWLIB_WDT_RANGES_HEADER(WOWLIB_WDT_HEADER_ALIAS)
#undef WOWLIB_WDT_HEADER_ALIAS
}

namespace wowlib::formats::wdt::occlusion
{
#define WOWLIB_WDT_OCCLUSION_ALIAS(Suffix, version_)                                               \
  using WDTOcclusion##Suffix = WDTOcclusion<versions::version_>;
  WOWLIB_WDT_RANGES_OCCLUSION(WOWLIB_WDT_OCCLUSION_ALIAS)
#undef WOWLIB_WDT_OCCLUSION_ALIAS
}

namespace wowlib::formats::wdt::lights
{
#define WOWLIB_WDT_LIGHTS_ALIAS(Suffix, version_)                                                  \
  using WDTLights##Suffix = WDTLights<versions::version_>;
  WOWLIB_WDT_RANGES_LIGHTS(WOWLIB_WDT_LIGHTS_ALIAS)
#undef WOWLIB_WDT_LIGHTS_ALIAS
}

namespace wowlib::formats::wdt::fogs
{
#define WOWLIB_WDT_FOGS_ALIAS(Suffix, version_)                                                    \
  using WDTFogs##Suffix = WDTFogs<versions::version_>;
  WOWLIB_WDT_RANGES_FOGS(WOWLIB_WDT_FOGS_ALIAS)
#undef WOWLIB_WDT_FOGS_ALIAS
}

namespace wowlib::formats::wdt::mpv
{
#define WOWLIB_WDT_MPV_ALIAS(Suffix, version_)                                                     \
  using WDTParticulates##Suffix = WDTParticulates<versions::version_>;
  WOWLIB_WDT_RANGES_MPV(WOWLIB_WDT_MPV_ALIAS)
#undef WOWLIB_WDT_MPV_ALIAS
}

namespace wowlib::formats::wdt
{
#define WOWLIB_WDT_ALIAS(Suffix, version_) using WDT##Suffix = WDT<versions::version_>;
  WOWLIB_WDT_RANGES_ASSEMBLY(WOWLIB_WDT_ALIAS)
#undef WOWLIB_WDT_ALIAS

  namespace detail
  {
    // Range-table validation: every family's rows must exactly enumerate the
    // distinct canonicals of its grid, with the suffix range_suffix derives.
#define WOWLIB_WDT_RANGE_ROW(Suffix, version_)                                                     \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array wdt_root_rows{WOWLIB_WDT_RANGES_ROOT(WOWLIB_WDT_RANGE_ROW)};
    static_assert(ranges_valid(wdt_root_rows, wdt_root_pivots, wdt_versions),
                  "WOWLIB_WDT_RANGES_ROOT drifted from wdt_root_pivots");
    inline constexpr std::array wdt_header_rows{WOWLIB_WDT_RANGES_HEADER(WOWLIB_WDT_RANGE_ROW)};
    static_assert(ranges_valid(wdt_header_rows, wdt_header_pivots, wdt_versions),
                  "WOWLIB_WDT_RANGES_HEADER drifted from wdt_header_pivots");
    inline constexpr std::array wdt_occlusion_rows{
      WOWLIB_WDT_RANGES_OCCLUSION(WOWLIB_WDT_RANGE_ROW)};
    static_assert(ranges_valid(wdt_occlusion_rows, wdt_occlusion_pivots, wdt_satellite_versions),
                  "WOWLIB_WDT_RANGES_OCCLUSION drifted from wdt_occlusion_pivots");
    inline constexpr std::array wdt_lights_rows{WOWLIB_WDT_RANGES_LIGHTS(WOWLIB_WDT_RANGE_ROW)};
    static_assert(ranges_valid(wdt_lights_rows, wdt_lights_pivots, wdt_satellite_versions),
                  "WOWLIB_WDT_RANGES_LIGHTS drifted from wdt_lights_pivots");
    inline constexpr std::array wdt_fogs_rows{WOWLIB_WDT_RANGES_FOGS(WOWLIB_WDT_RANGE_ROW)};
    static_assert(ranges_valid(wdt_fogs_rows, wdt_fogs_pivots, wdt_fogs_versions),
                  "WOWLIB_WDT_RANGES_FOGS drifted from wdt_fogs_pivots");
    inline constexpr std::array wdt_mpv_rows{WOWLIB_WDT_RANGES_MPV(WOWLIB_WDT_RANGE_ROW)};
    static_assert(ranges_valid(wdt_mpv_rows, wdt_mpv_pivots, wdt_mpv_versions),
                  "WOWLIB_WDT_RANGES_MPV drifted from wdt_mpv_pivots");
    inline constexpr std::array wdt_assembly_rows{
      WOWLIB_WDT_RANGES_ASSEMBLY(WOWLIB_WDT_RANGE_ROW)};
    static_assert(ranges_valid(wdt_assembly_rows, wdt_assembly_pivots, wdt_versions),
                  "WOWLIB_WDT_RANGES_ASSEMBLY drifted from wdt_assembly_pivots");
#undef WOWLIB_WDT_RANGE_ROW
  }
}
