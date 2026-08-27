#pragma once

/** @file
    WMO version pivots that change a binary-struct *layout* (not just chunk
    presence). These name the client version at which a struct's field set flips,
    so the binary structs (the group/chunks headers) can select a constrained
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

namespace wowlib::formats::wmo {
  /** The WMO format version every supported client uses (MVER payload). */
  inline constexpr std::uint32_t WmoVersionV17 = 17;

  /** legion 7.0.1.20740: SMOBatch's culling-box prelude gives way to a large
      (uint16) material id. A layout pivot for SMOBatch. */
  inline constexpr ClientVersion WmoBatchLargeMaterial = builds::Legion_Alpha;

  /** 9.2.0.42423: the unused u32 at MOGP+0x40 becomes the two split-group
      indices. A layout pivot for SMOGroupHeader. */
  inline constexpr ClientVersion WmoSplitGroups = builds::SL_EternitysEnd;

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
  inline constexpr std::array WmoVersions{
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

  /** SMOBatch: the culling-box prelude gives way to the large material id. */
  inline constexpr std::array WmoBatchPivots{WmoBatchLargeMaterial};

  /** SMOGroupHeader: the split-group indices replacing the unused u32. */
  inline constexpr std::array WmoGroupHeaderPivots{WmoSplitGroups};

  /** WMORoot: every trait-slot boundary and chunk-introduction build the
      root file carries (GFID/MOUV/MOSI/MODI/the volume family/new lights/
      M3 materials). */
  inline constexpr std::array WmoRootPivots{
    builds::Legion_Alpha,
    builds::Legion_ShadowsOfArgus_24473,
    builds::BfA_TidesOfVengeance,
    builds::BfA_VisionsOfNzoth_32044,
    builds::SL_Alpha_33978,
    builds::SL_ChainsOfDomination,
    builds::TWW_Alpha,
    builds::TWW_Undermined
  };

  /** WMOGroupBody / WMOGroup: every trait-slot boundary and chunk build the
      group files carry, plus the two binary-layout pivots above.

      MoP is a pivot because MDAL debuts there, NOT at WoD as wowdev has it
      ("could have been added earlier", flagged unverified): a 5.4.8 corpus
      sweep found 421 MDAL chunks, while 700 sampled Cata groups and 400
      WotLK groups carry none. Without the pivot, WMOGroupBody<mop> would
      canonicalize onto the Cata instantiation and the chunk would stay
      unmodelled for the era that introduced it. */
  inline constexpr std::array WmoGroupPivots{
    builds::Cata,
    builds::MoP,
    builds::WoD,
    WmoBatchLargeMaterial,
    builds::BfA_TidesOfVengeance,
    builds::BfA_VisionsOfNzoth_33775,
    builds::SL_Alpha_33978,
    WmoSplitGroups,
    builds::DF_Alpha
  };

  /** The WMO assembly: the union of the root and group pivots. */
  inline constexpr std::array WmoAssemblyPivots{
    builds::Cata,
    builds::MoP,
    builds::WoD,
    WmoBatchLargeMaterial,
    builds::Legion_ShadowsOfArgus_24473,
    builds::BfA_TidesOfVengeance,
    builds::BfA_VisionsOfNzoth_32044,
    builds::BfA_VisionsOfNzoth_33775,
    builds::SL_Alpha_33978,
    builds::SL_ChainsOfDomination,
    WmoSplitGroups,
    builds::DF_Alpha,
    builds::TWW_Alpha,
    builds::TWW_Undermined
  };
}
