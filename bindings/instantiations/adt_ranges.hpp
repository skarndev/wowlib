#pragma once

/** @file
    @brief Per-RANGE welded alias tables for the ADT template surface.

    Two versioned families: the ADT tile assembly and the MapChunk terrain cell.
    Each x-macro lists one row per REAL content permutation — x(Suffix, version)
    with the range's canonical grid version — and drives the welded aliases here
    plus the instantiation matrix in adt_matrix.inl. Both tables are
    consteval-checked against the family pivots (rangesValid in adt::detail
    below). Extending the version list means revisiting the pivot lists in
    formats/adt/boundaries.hpp; the checks then dictate the rows.

    Lives in the bindings, NOT the library: C++ consumers instantiate on demand
    and never need the flat per-range names — only welder does, because it welds
    a class-template instantiation through a namespace-scope alias whose
    identifier is the target-language name. */

#include <wowlib/formats/adt/adt.hpp>

// ADT assembly ranges (pivots TBC / WotLK / Cata / 8.1).
#define WOWLIB_ADT_RANGES_ASSEMBLY(x)                                                              \
  x(Vanilla, Vanilla)                                                                              \
  x(Tbc, Tbc)                                                                                      \
  x(Wotlk, Wotlk)                                                                                  \
  x(CataToLegion, Cata)                                                                            \
  x(BfaPlus, Bfa)

// MapChunk cell ranges (pivots WotLK / Cata).
#define WOWLIB_ADT_RANGES_MAPCHUNK(x)                                                              \
  x(VanillaToTbc, Vanilla)                                                                         \
  x(Wotlk, Wotlk)                                                                                  \
  x(CataPlus, Cata)

// The bindings surface for the versioned templates: welder welds a
// class-template instantiation through a namespace-scope alias whose identifier
// is the target-language name. Both families surface under the adt submodule.
namespace wowlib::formats::adt
{
#define WOWLIB_ADT_ASSEMBLY_ALIAS(Suffix, version_) using ADT##Suffix = ADT<versions::version_>;
  WOWLIB_ADT_RANGES_ASSEMBLY(WOWLIB_ADT_ASSEMBLY_ALIAS)
#undef WOWLIB_ADT_ASSEMBLY_ALIAS

#define WOWLIB_ADT_MAPCHUNK_ALIAS(Suffix, version_)                                                \
  using MapChunk##Suffix = MapChunk<versions::version_>;
  WOWLIB_ADT_RANGES_MAPCHUNK(WOWLIB_ADT_MAPCHUNK_ALIAS)
#undef WOWLIB_ADT_MAPCHUNK_ALIAS

  namespace detail
  {
    // Range-table validation: each family's rows must exactly enumerate the
    // distinct canonicals of its grid, with the suffix rangeSuffix derives.
#define WOWLIB_ADT_RANGE_ROW(Suffix, version_)                                                     \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array AdtAssemblyRows{WOWLIB_ADT_RANGES_ASSEMBLY(WOWLIB_ADT_RANGE_ROW)};
    static_assert(rangesValid(AdtAssemblyRows, AdtPivots, AdtVersions),
                  "WOWLIB_ADT_RANGES_ASSEMBLY drifted from adt_pivots");
    inline constexpr std::array AdtMapchunkRows{WOWLIB_ADT_RANGES_MAPCHUNK(WOWLIB_ADT_RANGE_ROW)};
    static_assert(rangesValid(AdtMapchunkRows, MapChunkPivots, AdtVersions),
                  "WOWLIB_ADT_RANGES_MAPCHUNK drifted from map_chunk_pivots");
#undef WOWLIB_ADT_RANGE_ROW
  }
}
