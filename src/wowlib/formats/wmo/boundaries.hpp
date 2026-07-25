#pragma once

/** @file
    WMO version pivots that change a wire-struct *layout* (not just chunk
    presence). These name the client version at which a struct's field set flips,
    so the wire structs (the group/chunks headers) can select a constrained
    partial specialization with `requires (V < pivot)` / `requires (V >= pivot)`.

    Chunk *presence* boundaries are no longer named here: each chunk member's
    since()/until() annotation now carries its own exact client version inline (as
    wowdev.wiki documents it), instead of grouping chunks under a shared,
    guessed-at "feature set" constant. */

#include <array>
#include <cstdint>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

namespace wowlib::formats::wmo
{
  /** The WMO format version every supported client uses (MVER payload). */
  inline constexpr std::uint32_t wmo_version_v17 = 17;

  /** Legion 7.0.1.20740: SMOBatch's culling-box prelude gives way to a large
      (uint16) material id. A layout pivot for SMOBatch. */
  inline constexpr ClientVersion wmo_batch_large_material = builds::LegionAlpha;

  /** 9.2.0.42423: the unused u32 at MOGP+0x40 becomes the two split-group
      indices. A layout pivot for SMOGroupHeader. */
  inline constexpr ClientVersion wmo_split_groups = builds::EternitysEnd;

  // --- version grid and per-family canonicalization pivots ------------------
  //
  // Every versioned WMO family instantiates only for its REAL content
  // permutations (see formats/common/version_range.hpp): the public name is
  // a canonicalizing alias flooring the requested version over the family's
  // pivots. The lists are generous — every documented change point, not just
  // the ones the current grid straddles; the welded range tables in wmo.hpp
  // are consteval-checked against them.

  /** The versions WMO is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. */
  inline constexpr std::array wmo_versions{
    versions::vanilla, versions::tbc,         versions::wotlk,
    versions::cata,    versions::mop,         versions::wod,
    versions::legion,  versions::bfa,         versions::shadowlands,
    versions::dragonflight, versions::tww};

  /** SMOBatch: the culling-box prelude gives way to the large material id. */
  inline constexpr std::array wmo_batch_pivots{wmo_batch_large_material};

  /** SMOGroupHeader: the split-group indices replacing the unused u32. */
  inline constexpr std::array wmo_group_header_pivots{wmo_split_groups};

  /** WMORoot: every trait-slot boundary and chunk-introduction build the
      root file carries (GFID/MOUV/MOSI/MODI/the volume family/new lights/
      M3 materials). */
  inline constexpr std::array wmo_root_pivots{
    builds::LegionAlpha, builds::ShadowsOfArgus_24473,
    builds::TidesOfVengeance, builds::VisionsOfNzoth_32044,
    builds::ShadowlandsAlpha_33978, builds::ChainsOfDomination,
    builds::TheWarWithinAlpha, builds::Undermined};

  /** WMOGroupBody / WMOGroup: every trait-slot boundary and chunk build the
      group files carry, plus the two wire-layout pivots above. */
  inline constexpr std::array wmo_group_pivots{
    builds::Cataclysm,      builds::WarlordsOfDraenor,
    wmo_batch_large_material,       builds::TidesOfVengeance,
    builds::VisionsOfNzoth_33775,  builds::ShadowlandsAlpha_33978,
    wmo_split_groups,               builds::DragonflightAlpha};

  /** The WMO assembly: the union of the root and group pivots. */
  inline constexpr std::array wmo_assembly_pivots{
    builds::Cataclysm,      builds::WarlordsOfDraenor,
    wmo_batch_large_material,       builds::ShadowsOfArgus_24473,
    builds::TidesOfVengeance,  builds::VisionsOfNzoth_32044,
    builds::VisionsOfNzoth_33775,  builds::ShadowlandsAlpha_33978,
    builds::ChainsOfDomination,  wmo_split_groups,
    builds::DragonflightAlpha, builds::TheWarWithinAlpha,
    builds::Undermined};
}
