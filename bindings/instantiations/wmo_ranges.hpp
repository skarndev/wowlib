#pragma once

/** @file
    @brief Per-RANGE welded alias tables for the WMO template surface.

    Each family's x-macro lists one row per REAL content permutation —
    x(Suffix, version) with the range's canonical grid version — and drives
    the welded aliases here plus the instantiation matrix in wmo_matrix.inl.
    Every table is consteval-checked against the family's pivots
    (rangesValid in wmo::detail below). Extending the version list means
    revisiting the pivot lists in formats/wmo/boundaries.hpp; the checks then
    dictate the rows.

    This lives in the bindings, NOT the library: C++ consumers instantiate
    the templates on demand and never need the flat per-range names — only
    welder does, because it welds a class-template instantiation through a
    namespace-scope alias whose identifier is the target-language name. */

#include <wowlib/formats/wmo/wmo.hpp>

#define WOWLIB_WMO_RANGES_ROOT(x)                                                                  \
  x(VanillaToWod, Vanilla)                                                                         \
  x(Legion, Legion)                                                                                \
  x(Bfa, Bfa)                                                                                      \
  x(ShadowlandsToDragonflight, Shadowlands)                                                        \
  x(TheWarWithin, Tww)

#define WOWLIB_WMO_RANGES_GROUP(x)                                                                 \
  x(VanillaToWotlk, Vanilla)                                                                       \
  x(Cata, Cata)                                                                                    \
  x(Mop, Mop)                                                                                      \
  x(Wod, Wod)                                                                                      \
  x(Legion, Legion)                                                                                \
  x(Bfa, Bfa)                                                                                      \
  x(Shadowlands, Shadowlands)                                                                      \
  x(DragonflightPlus, Dragonflight)

#define WOWLIB_WMO_RANGES_GROUP_HEADER(x)                                                          \
  x(VanillaToBfa, Vanilla)                                                                         \
  x(ShadowlandsPlus, Shadowlands)

#define WOWLIB_WMO_RANGES_BATCH(x)                                                                 \
  x(VanillaToWod, Vanilla)                                                                         \
  x(LegionPlus, Legion)

#define WOWLIB_WMO_RANGES_ASSEMBLY(x)                                                              \
  x(VanillaToWotlk, Vanilla)                                                                       \
  x(Cata, Cata)                                                                                    \
  x(Mop, Mop)                                                                                      \
  x(Wod, Wod)                                                                                      \
  x(Legion, Legion)                                                                                \
  x(Bfa, Bfa)                                                                                      \
  x(Shadowlands, Shadowlands)                                                                      \
  x(Dragonflight, Dragonflight)                                                                    \
  x(TheWarWithin, Tww)

// The bindings surface for the versioned templates: welder welds a
// class-template instantiation through a namespace-scope alias, whose
// identifier is the target-language name. Each family's aliases are declared
// in its own namespace so the per-range classes surface under the matching
// submodule, mirroring the C++ layout.
namespace wowlib::formats::wmo::root
{
#define WOWLIB_WMO_ROOT_ALIAS(Suffix, version_) using WMORoot##Suffix = WMORoot<versions::version_>;
  WOWLIB_WMO_RANGES_ROOT(WOWLIB_WMO_ROOT_ALIAS)
#undef WOWLIB_WMO_ROOT_ALIAS
}

namespace wowlib::formats::wmo::group
{
#define WOWLIB_WMO_GROUP_ALIAS(Suffix, version_)                                                   \
  using WMOGroupBody##Suffix = WMOGroupBody<versions::version_>;                                   \
  using WMOGroup##Suffix = WMOGroup<versions::version_>;
  WOWLIB_WMO_RANGES_GROUP(WOWLIB_WMO_GROUP_ALIAS)
#undef WOWLIB_WMO_GROUP_ALIAS
}

namespace wowlib::formats::wmo::group::chunks
{
#define WOWLIB_WMO_GROUP_HEADER_ALIAS(Suffix, version_)                                            \
  using WMOGroupHeader##Suffix = SMOGroupHeader<versions::version_>;
  WOWLIB_WMO_RANGES_GROUP_HEADER(WOWLIB_WMO_GROUP_HEADER_ALIAS)
#undef WOWLIB_WMO_GROUP_HEADER_ALIAS

#define WOWLIB_WMO_BATCH_ALIAS(Suffix, version_)                                                   \
  using WMOBatch##Suffix = SMOBatch<versions::version_>;
  WOWLIB_WMO_RANGES_BATCH(WOWLIB_WMO_BATCH_ALIAS)
#undef WOWLIB_WMO_BATCH_ALIAS
}

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_ALIAS(Suffix, version_) using WMO##Suffix = WMO<versions::version_>;
  WOWLIB_WMO_RANGES_ASSEMBLY(WOWLIB_WMO_ALIAS)
#undef WOWLIB_WMO_ALIAS

  namespace detail
  {
    // Range-table validation: every family's rows must exactly enumerate the
    // distinct canonicals of the grid, with the suffix rangeSuffix derives.
#define WOWLIB_WMO_RANGE_ROW(Suffix, version_)                                                     \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array WmoRootRows{WOWLIB_WMO_RANGES_ROOT(WOWLIB_WMO_RANGE_ROW)};
    static_assert(rangesValid(WmoRootRows, WmoRootPivots, WmoVersions),
                  "WOWLIB_WMO_RANGES_ROOT drifted from wmo_root_pivots");
    inline constexpr std::array WmoGroupRows{WOWLIB_WMO_RANGES_GROUP(WOWLIB_WMO_RANGE_ROW)};
    static_assert(rangesValid(WmoGroupRows, WmoGroupPivots, WmoVersions),
                  "WOWLIB_WMO_RANGES_GROUP drifted from wmo_group_pivots");
    inline constexpr std::array WmoGroupHeaderRows{
      WOWLIB_WMO_RANGES_GROUP_HEADER(WOWLIB_WMO_RANGE_ROW)};
    static_assert(rangesValid(WmoGroupHeaderRows, WmoGroupHeaderPivots, WmoVersions),
                  "WOWLIB_WMO_RANGES_GROUP_HEADER drifted from wmo_group_header_pivots");
    inline constexpr std::array WmoBatchRows{WOWLIB_WMO_RANGES_BATCH(WOWLIB_WMO_RANGE_ROW)};
    static_assert(rangesValid(WmoBatchRows, WmoBatchPivots, WmoVersions),
                  "WOWLIB_WMO_RANGES_BATCH drifted from wmo_batch_pivots");
    inline constexpr std::array WmoAssemblyRows{
      WOWLIB_WMO_RANGES_ASSEMBLY(WOWLIB_WMO_RANGE_ROW)};
    static_assert(rangesValid(WmoAssemblyRows, WmoAssemblyPivots, WmoVersions),
                  "WOWLIB_WMO_RANGES_ASSEMBLY drifted from wmo_assembly_pivots");
#undef WOWLIB_WMO_RANGE_ROW
  }
}
