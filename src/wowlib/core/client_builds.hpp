#pragma once

/** @file
    Named ClientVersion constants for the exact client builds format features
    appeared (or vanished) at — the vocabulary the =since()/=until() chunk
    annotations and the canonicalization pivot lists spell their versions
    with, instead of repeating raw ClientVersion{...} literals.

    Two kinds of constants live here:
    - *Era markers* (build 0): "any client of this expansion" — used when a
      feature is documented only down to the expansion, not a build.
    - *Exact builds*: the alpha/beta/PTR/release build wowdev.wiki dates a
      chunk or layout change to, named after the patch's title. When several
      builds of one patch are referenced, the build number disambiguates
      (e.g. ShadowsOfArgus_24473 vs ShadowsOfArgus_24500). */

#include <wowlib/core/client_version.hpp>

namespace wowlib::builds
{
  // --- era markers (build 0: "any client of this expansion/patch") ----------

  /** The Burning Crusade (any 2.x client). */
  inline constexpr ClientVersion TheBurningCrusade{2, 0, 0, 0};

  /** Wrath of the Lich King (any 3.x client). */
  inline constexpr ClientVersion WrathOfTheLichKing{3, 0, 0, 0};

  /** Cataclysm (any 4.x client). */
  inline constexpr ClientVersion Cataclysm{4, 0, 0, 0};

  /** Warlords of Draenor (any 6.x client). */
  inline constexpr ClientVersion WarlordsOfDraenor{6, 0, 0, 0};

  /** Legion (any 7.x client). */
  inline constexpr ClientVersion Legion{7, 0, 0, 0};

  /** Battle for Azeroth (any 8.x client). */
  inline constexpr ClientVersion BattleForAzeroth{8, 0, 1, 0};

  /** Dragonflight (any 10.x client). */
  inline constexpr ClientVersion Dragonflight{10, 0, 0, 0};

  // --- exact builds, in release order ----------------------------------------

  /** Legion alpha, 7.0.1 build 20740 — the first chunked-format build (the
      .m2 MD21 container, WMO GFID, the SMOBatch large-material layout). */
  inline constexpr ClientVersion LegionAlpha{7, 0, 1, 20740};

  /** Shadows of Argus PTR, 7.3.0 build 24473. */
  inline constexpr ClientVersion ShadowsOfArgus_24473{7, 3, 0, 24473};

  /** Shadows of Argus PTR, 7.3.0 build 24500 (the .m2 parent-model and .skel
      satellite chunks). */
  inline constexpr ClientVersion ShadowsOfArgus_24500{7, 3, 0, 24500};

  /** Battle for Azeroth beta, 8.0.1 build 26629 (TXID/LDV1). */
  inline constexpr ClientVersion BfaBeta{8, 0, 1, 26629};

  /** Tides of Vengeance PTR, 8.1.0 build 27826 (RPID/GPID, WMO MOSI/MODI). */
  inline constexpr ClientVersion TidesOfVengeance{8, 1, 0, 27826};

  /** Rise of Azshara, 8.2.0 build 30080 (WFV1/WFV2/PGD1). */
  inline constexpr ClientVersion RiseOfAzshara{8, 2, 0, 30080};

  /** Visions of N'Zoth PTR, 8.3.0 build 32044. */
  inline constexpr ClientVersion VisionsOfNzoth_32044{8, 3, 0, 32044};

  /** Visions of N'Zoth, 8.3.0 build 33775 (WMO MPVR). */
  inline constexpr ClientVersion VisionsOfNzoth_33775{8, 3, 0, 33775};

  /** Shadowlands alpha, 9.0.1 build 33978 (WFV3/PFDC/EDGF/NERF/DBOC). */
  inline constexpr ClientVersion ShadowlandsAlpha_33978{9, 0, 1, 33978};

  /** Shadowlands alpha, 9.0.1 build 34365 (DETL). */
  inline constexpr ClientVersion ShadowlandsAlpha_34365{9, 0, 1, 34365};

  /** Chains of Domination PTR, 9.1.0 build 39015. */
  inline constexpr ClientVersion ChainsOfDomination{9, 1, 0, 39015};

  /** Eternity's End, 9.2.0 build 42423 (the MOGP split-group indices). */
  inline constexpr ClientVersion EternitysEnd{9, 2, 0, 42423};

  /** Dragonflight alpha, 10.0.0 build 46181. */
  inline constexpr ClientVersion DragonflightAlpha{10, 0, 0, 46181};

  /** The War Within alpha, 11.0.0 build 54210. */
  inline constexpr ClientVersion TheWarWithinAlpha{11, 0, 0, 54210};

  /** Undermine(d) PTR, 11.1.0 build 58221. */
  inline constexpr ClientVersion Undermined{11, 1, 0, 58221};

  /** Legacy of Arathor, 11.1.7 build 60520 (PCOL/DPIV). */
  inline constexpr ClientVersion LegacyOfArathor{11, 1, 7, 60520};
}
