#pragma once

/** @file
    The _occ.wdt occlusion satellite entity (namespace
    wowlib::formats::wdt::occlusion), WoD+: low-resolution heightmaps that
    occlude everything behind them, one per ADT tile. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>
#include <wowlib/formats/wdt/occlusion/chunks/records.hpp>

namespace wowlib::formats::wdt::occlusion
{
  using namespace wowlib::formats::wdt::occlusion::chunks;

  /** The version-agnostic base of every WDTOcclusion<V> (welded as
      "WDTOcclusion"); bindings-only, like every *Base.
      @see https://wowdev.wiki/WDT#occ */
  struct [[
    =welder::weld,
    =welder::weld_as("WDTOcclusion"),
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        A _occ.wdt occlusion satellite (WoD+), abstract over the client
        version: per-tile low-resolution heightmaps the renderer occludes
        against. Construct a concrete version with
        WDTOcclusion.for_version(expansion). See https://wowdev.wiki/WDT.)")
  ]] WDTOcclusionBase
  {
  };

  namespace detail
  {
    /** A _occ.wdt occlusion satellite for one client version (WoD+): the
        MAOI tile index and the packed MAOH heightmap block. Both chunks are
        present but empty for WMO-only maps. Instantiate through the
        canonicalizing wdt::occlusion::WDTOcclusion alias, never directly.
        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WDT#occ */
    template <ClientVersion V>
    struct [[
      =welder::weld,
      =welder::doc(R"(
          A _occ.wdt occlusion satellite for one client version (WoD+):
          per-tile occlusion heightmaps. Each MAOI index record locates one
          tile's 545 int16 values inside the packed heights block. See
          https://wowdev.wiki/WDT.)")
    ]] WDTOcclusion : ChunkedFile<WDTOcclusion<V>>, WDTOcclusionBase
    {
      static constexpr ClientVersion version = V;

      [[
        =chunk("MVER"),
        =welder::doc("The file format version; 18 like the main WDT.")]]
      std::uint32_t mver = wdt_version_18;

      [[
        =chunk("MAOI"),
        =welder::mark::no_reassign,
        =welder::doc(R"(The tile index (MAOI): one record per tile with occlusion
                        data, locating its heightmap inside heights. Empty for
                        WMO-only maps.)")]]
      std::vector<OcclusionIndex> indices;

      [[
        =chunk("MAOH"),
        =welder::mark::no_reassign,
        =welder::doc(R"(The packed heightmap block (MAOH): the tiles' interleaved
                        17x17 + 16x16 int16 grids back to back, addressed by the
                        index records' offsets (same layout as the WDL MARE
                        payload). Empty for WMO-only maps.)")]]
      std::vector<std::int16_t> heights;
    };
  }

  /** A _occ.wdt satellite — the canonicalizing face of detail::WDTOcclusion:
      stable since WoD, so a single instantiation serves every release. */
  template <ClientVersion V>
    requires(V >= builds::WoD)
  using WDTOcclusion = detail::WDTOcclusion<
    canonical_version(V, wdt_occlusion_pivots, wdt_satellite_versions)>;
}
