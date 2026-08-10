#pragma once

/** @file
    @brief Per-RANGE welded alias tables for the WMO template surface.

    Each family's X-macro lists one row per REAL content permutation —
    X(Suffix, version) with the range's canonical grid version — and drives
    the welded aliases here plus the instantiation matrix in wmo_matrix.inl.
    Every table is consteval-checked against the family's pivots
    (ranges_valid in wmo::detail below). Extending the version list means
    revisiting the pivot lists in formats/wmo/boundaries.hpp; the checks then
    dictate the rows.

    This lives in the bindings, NOT the library: C++ consumers instantiate
    the templates on demand and never need the flat per-range names — only
    welder does, because it welds a class-template instantiation through a
    namespace-scope alias whose identifier is the target-language name. */

#include <wowlib/formats/wmo/wmo.hpp>

#define WOWLIB_WMO_RANGES_ROOT(X)                                                                  \
  X(VanillaToWod, vanilla)                                                                         \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(ShadowlandsToDragonflight, shadowlands)                                                        \
  X(TheWarWithin, tww)

#define WOWLIB_WMO_RANGES_GROUP(X)                                                                 \
  X(VanillaToWotlk, vanilla)                                                                       \
  X(Cata, cata)                                                                                    \
  X(Mop, mop)                                                                                      \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(DragonflightPlus, dragonflight)

#define WOWLIB_WMO_RANGES_GROUP_HEADER(X)                                                          \
  X(VanillaToBfa, vanilla)                                                                         \
  X(ShadowlandsPlus, shadowlands)

#define WOWLIB_WMO_RANGES_BATCH(X)                                                                 \
  X(VanillaToWod, vanilla)                                                                         \
  X(LegionPlus, legion)

#define WOWLIB_WMO_RANGES_ASSEMBLY(X)                                                              \
  X(VanillaToWotlk, vanilla)                                                                       \
  X(Cata, cata)                                                                                    \
  X(Mop, mop)                                                                                      \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

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
    // distinct canonicals of the grid, with the suffix range_suffix derives.
#define WOWLIB_WMO_RANGE_ROW(Suffix, version_)                                                     \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array wmo_root_rows{WOWLIB_WMO_RANGES_ROOT(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_root_rows, wmo_root_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_ROOT drifted from wmo_root_pivots");
    inline constexpr std::array wmo_group_rows{WOWLIB_WMO_RANGES_GROUP(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_group_rows, wmo_group_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_GROUP drifted from wmo_group_pivots");
    inline constexpr std::array wmo_group_header_rows{
      WOWLIB_WMO_RANGES_GROUP_HEADER(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_group_header_rows, wmo_group_header_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_GROUP_HEADER drifted from wmo_group_header_pivots");
    inline constexpr std::array wmo_batch_rows{WOWLIB_WMO_RANGES_BATCH(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_batch_rows, wmo_batch_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_BATCH drifted from wmo_batch_pivots");
    inline constexpr std::array wmo_assembly_rows{
      WOWLIB_WMO_RANGES_ASSEMBLY(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_assembly_rows, wmo_assembly_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_ASSEMBLY drifted from wmo_assembly_pivots");
#undef WOWLIB_WMO_RANGE_ROW
  }
}
