#pragma once

/** @file
    WDT version grids and per-family canonicalization pivots. The main file's
    only *layout* pivot is 8.1.0.28294, where the MPHD header repurposes its
    unused tail into the satellite FileDataIDs; everything else is chunk
    presence, carried inline by the members' since()/until() annotations.

    The satellite files (_occ/_lgt/_fogs/_mpv) appeared mid-timeline, so each
    declares an era-subset GRID starting at its first release alongside its
    pivots; the assembly's pivot list is the union of every boundary that
    changes what a WDT<V> carries. */

#include <array>
#include <cstdint>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

namespace wowlib::formats::wdt {
  /** The WDT format version every supported client uses (the .wdt and
      _occ.wdt MVER payload; _lgt/_fogs/_mpv carry their own numbering). */
  inline constexpr std::uint32_t WdtVersion18 = 18;

  /** The tile-table slot count of a map: the 64x64 grid MAIN (and, since 8.1,
      MAID) covers, row-major with y outer — the client indexes it
      positionally, so the size is part of the format. */
  inline constexpr std::size_t WdtTileSlots = 64 * 64;

  /** 8.1.0.28294: the MPHD unused tail becomes the satellite FileDataIDs and
      MAID arrives — the namehash-removal preparation. A layout pivot for
      SMMapHeader. */
  inline constexpr ClientVersion WdtMapFdids = builds::BfA_TidesOfVengeance_28294;

  // --- version grids ---------------------------------------------------------

  /** The versions WDT is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. */
  inline constexpr std::array WdtVersions{
    versions::Vanilla,
    versions::Tbc,
    versions::Wotlk,
    versions::Cata,
    versions::Mop,
    versions::Wod,
    versions::Legion,
    versions::Bfa,
    versions::Shadowlands,
    versions::Dragonflight,
    versions::Tww
  };

  /** The _occ/_lgt satellites exist since WoD: their era-subset grid. */
  inline constexpr std::array WdtSatelliteVersions{
    versions::Wod,
    versions::Legion,
    versions::Bfa,
    versions::Shadowlands,
    versions::Dragonflight,
    versions::Tww
  };

  /** The _fogs satellite exists since Legion 7.2.5: its era-subset grid. */
  inline constexpr std::array WdtFogsVersions{
    versions::Legion,
    versions::Bfa,
    versions::Shadowlands,
    versions::Dragonflight,
    versions::Tww
  };

  /** The _mpv satellite exists since BfA 8.0.1: its era-subset grid. */
  inline constexpr std::array WdtMpvVersions{
    versions::Bfa,
    versions::Shadowlands,
    versions::Dragonflight,
    versions::Tww
  };

  // --- per-family canonicalization pivots -------------------------------------

  /** SMMapHeader: the MPHD FileDataID relayout. */
  inline constexpr std::array WdtHeaderPivots{WdtMapFdids};

  /** WDTRoot: the MPHD/MAID pivot plus MANM (the Shadowlands map anima). */
  inline constexpr std::array WdtRootPivots{WdtMapFdids, builds::SL_Alpha_33978};

  /** WDTOcclusion: stable since its WoD introduction (no pivots). */
  inline constexpr std::array<ClientVersion, 0> WdtOcclusionPivots{};

  /** WDTLights: the Legion light-set replacement (MPLT out; MPL2/MSLT/MTEX/
      MLTA in; the MVER payload bumps to 20) and the Shadowlands MPL3. */
  inline constexpr std::array WdtLightsPivots{builds::Legion, builds::Legion_Alpha_20914, builds::SL_Beta_34490};

  /** WDTFogs: the first VFOG content builds and the version-2 VFEX records. */
  inline constexpr std::array WdtFogsPivots{builds::BfA_Beta_25902, builds::TWW_Alpha_54935};

  /** WDTParticulates: stable since its BfA introduction — the PVMI record
      size is keyed on the file's OWN version payload, not the client (kept
      opaque), so no client-version pivot exists. */
  inline constexpr std::array<ClientVersion, 0> WdtMpvPivots{};

  /** The WDT assembly: the union of the root and satellite pivots plus each
      satellite file's introduction build. */
  inline constexpr std::array WdtAssemblyPivots{
    builds::WoD,
    builds::Legion,
    builds::Legion_Alpha_20914,
    builds::Legion_TombOfSargeras,
    builds::BfA_Beta_25902,
    builds::BfA_Beta_26287,
    WdtMapFdids,
    builds::SL_Alpha_33978,
    builds::SL_Beta_34490,
    builds::TWW_Alpha_54935
  };
}
