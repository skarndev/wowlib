#pragma once

/** @file
    ADT version grids, layout pivots and the alpha-format context.

    ADT is ONE version-agnostic entity (`ADT<V>`, see adt.hpp): the split into
    physical files (root/_tex0/_obj0/_obj1/_lod, Cata+) is a serializer concern,
    not a class split. Almost every version difference is chunk PRESENCE, carried
    inline by the members' since()/until() annotations; the pivots below are the
    union of every boundary that changes what an ADT<V> (or MapChunk<V>) carries,
    used only to collapse the instantiation matrix to its real permutations. */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

namespace wowlib::formats::adt {
  /** The ADT format version every supported client writes (the MVER payload). */
  inline constexpr std::uint32_t AdtVersion18 = 18;

  /** The terrain chunks a tile is divided into: a 16x16 grid, indexed
      row-major (y * 16 + x) — the client addresses them positionally, so the
      count is fixed for every version. */
  inline constexpr std::size_t ChunksPerTile = 16 * 16;

  // --- layout pivots (documented boundaries that change ADT<V> content) -------

  /** 8.1.0.27826: MDID/MHID diffuse+height FileDataID tables arrive in _tex0
      (MTEX still present this one build). */
  inline constexpr ClientVersion AdtTexFdids = builds::BfA_TidesOfVengeance_28294;

  /** Cataclysm: ADTs split into root/_tex0/_obj0 (+_obj1); MCIN gone, MCLV /
      MCRD / MCRW / MAMP / MCMT arrive. */
  inline constexpr ClientVersion AdtSplit = builds::Cata;

  // --- version grids ----------------------------------------------------------

  /** The versions ADT is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. */
  inline constexpr std::array AdtVersions{
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

  // --- per-family canonicalization pivots -------------------------------------
  // Listed generously; a pivot that does not separate two grid versions is a
  // no-op (version_range.hpp). Each names a build where an ADT<V> or MapChunk<V>
  // member first appears or vanishes.

  /** MapChunk<V> content pivots — only boundaries that change what a chunk
      CURRENTLY carries: WotLK (MCCV vertex colors arrive) and Cata (MCLV baked
      lighting + MCMT materials arrive, and the legacy MCLQ liquid is finally
      gone). The legacy MCLQ liquid is NOT removed at WotLK — Outland tiles keep
      shipping it in 3.3.5a and the exact removal build is unknown, so it stays
      available through WotLK (see map_chunk.hpp). MoP MCBB/MPTX and later
      per-chunk sub-chunks are not modeled yet; add their pivots when they are (a
      no-op pivot would just over-instantiate). Ranges: VanillaToTbc / wotlk /
      CataPlus. */
  inline constexpr std::array MapChunkPivots{builds::WotLK, builds::Cata};

  /** ADT<V> assembly content pivots — only boundaries that change what the tile
      CURRENTLY carries: TBC (MFBO), WotLK (MH2O/MTXF/MCCV), Cata (split files,
      MAMP, obj1/lod), 8.1 (MDID/MHID FileDataID texture tables). MoP/WoD add no
      modeled tile chunks, and _obj1/_lod stay raw until stage 3, so Legion and
      SL are not yet pivots. Ranges: Vanilla / tbc / wotlk / CataToLegion /
      BfaPlus. */
  inline constexpr std::array AdtPivots{builds::TBC, builds::WotLK, builds::Cata, AdtTexFdids};

  // --- alpha-format context ---------------------------------------------------

  enum class [[
      =welder::weld,
      =welder::doc(R"(
        The on-disk MCAL alpha-map bit depth for a tile, decided by the map's
        WDT MPHD flags (adt_has_big_alpha 0x4 / adt_has_height_texturing 0x80),
        not the client era. wowlib always decodes alpha maps to a 64x64 8-bit
        edit surface; this records how they were laid out on disk so a write can
        reproduce the same encoding. RLE compression is an independent per-layer
        choice (the MCLY alpha_map_compressed flag), orthogonal to the depth.)")
    ]] AlphaFormat : std::uint8_t {
    Lowres4Bit [[=welder::doc(
      "2048-byte 4-bit alpha maps (the default when the WDT "
      "sets neither big-alpha flag).")]] = 0,
    Highres8Bit [[=welder::doc(
      "4096-byte 8-bit alpha maps (the WDT has adt_has_big_alpha "
      "or adt_has_height_texturing).")]] = 1
  };
}
