#pragma once

/** @file
    WDL version grid and canonicalization pivots. Everything is chunk
    presence (carried inline by the members' since()/until() annotations) —
    the WDL never changed a binary-struct layout:
    - TBC adds the per-tile MAHO hole masks (verified: vanilla 1.12 WDLs carry
      MARE heightmaps with zero MAHO, every 2.4.3 WDL pairs one MAHO per MARE —
      wowdev.wiki dates MAHO to WotLK, which our client scan disproves);
    - Legion swaps the object set (MWMO/MWID/MODF give way to the
      MLDD/MLDX/MLMD/MLMX low-resolution placements — though WMO-only maps
      keep shipping the old three, so they carry no until()), drops MAOC and
      adds MAOE;
    - BfA adds the per-placement MLDF fade distances and the MLMB byte
      (both undocumented on wowdev, which had them misdated to Shadowlands —
      the 8.3.7 fleet survey carries 33 MLDF and 4 MLMB files);
    - Shadowlands adds the sky-scene chunks (MSSN/MSSC/MSSO/MSSF) and a few
      undocumented blobs (MLDL/MLDB);
    - The War Within adds MSLD/MSLI. */

#include <array>
#include <cstdint>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

namespace wowlib::formats::wdl {
  /** The WDL format version every supported client uses (MVER payload). */
  inline constexpr std::uint32_t wdl_version_18 = 18;

  /** The MAOF tile table's slot count: one per map tile of the 64x64 grid,
      row-major, a zero offset meaning the tile has no heightmap. */
  inline constexpr std::size_t wdl_tile_slots = 64 * 64;

  /** The versions WDL is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. */
  inline constexpr std::array wdl_versions{
    versions::vanilla,
    versions::tbc,
    versions::wotlk,
    versions::cata,
    versions::mop,
    versions::wod,
    versions::legion,
    versions::bfa,
    versions::shadowlands,
    versions::dragonflight,
    versions::tww
  };

  /** WDL: every chunk-set change point (see the file comment). */
  inline constexpr std::array wdl_pivots{builds::TBC, builds::Legion, builds::BfA, builds::SL, builds::TWW};
}
