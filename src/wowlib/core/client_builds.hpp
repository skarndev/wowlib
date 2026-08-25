#pragma once

/** @file
    Named ClientVersion constants for the exact client builds format features
    appeared (or vanished) at — the vocabulary the =since()/=until() chunk
    annotations and the canonicalization pivot lists spell their versions
    with, instead of repeating raw ClientVersion{...} literals.

    Two kinds of constants live here, both prefixed with the expansion's
    SHORT community name (TBC, WotLK, Cata, WoD, Legion, BfA, SL, DF, TWW)
    so a reader always knows which era a build belongs to:
    - *Era markers* (build 0): just the expansion name — "any client of this
      expansion" — used when a feature is documented only down to the
      expansion, not a build.
    - *Exact builds*: `<Expansion>_<PatchTitle>` — the alpha/beta/PTR/release
      build wowdev.wiki dates a chunk or layout change to, named after the
      patch's title. When several builds of one patch are referenced, the
      build number disambiguates (Legion_ShadowsOfArgus_24473 vs _24500). */

#include <wowlib/core/client_version.hpp>

namespace wowlib::builds {
  // --- era markers (build 0: "any client of this expansion") ----------------

  /** The Burning Crusade (any 2.x client). */
  inline constexpr ClientVersion TBC{2, 0, 0, 0};

  /** Wrath of the Lich King (any 3.x client). */
  inline constexpr ClientVersion WotLK{3, 0, 0, 0};

  /** Cataclysm (any 4.x client). */
  inline constexpr ClientVersion Cata{4, 0, 0, 0};

  /** Mists of Pandaria (any 5.x client). */
  inline constexpr ClientVersion MoP{5, 0, 0, 0};

  /** Warlords of Draenor (any 6.x client). */
  inline constexpr ClientVersion WoD{6, 0, 0, 0};

  /** Legion (any 7.x client). */
  inline constexpr ClientVersion Legion{7, 0, 0, 0};

  /** Battle for Azeroth (any 8.x client). */
  inline constexpr ClientVersion BfA{8, 0, 1, 0};

  /** Shadowlands (any 9.x client). */
  inline constexpr ClientVersion SL{9, 0, 0, 0};

  /** Dragonflight (any 10.x client). */
  inline constexpr ClientVersion DF{10, 0, 0, 0};

  /** The War Within (any 11.x client). */
  inline constexpr ClientVersion TWW{11, 0, 0, 0};

  // --- exact builds, in release order ----------------------------------------

  /** Legion alpha, 7.0.1 build 20740 — the first chunked-format build (the
      .m2 MD21 container, WMO GFID, the SMOBatch large-material layout). */
  inline constexpr ClientVersion Legion_Alpha{7, 0, 1, 20740};

  /** Legion alpha, 7.0.1 build 20914 (the _lgt.wdt MVER payload bumps
      18 -> 20). */
  inline constexpr ClientVersion Legion_Alpha_20914{7, 0, 1, 20914};

  /** Legion, Tomb of Sargeras, 7.2.5 build 24076 (the _fogs.wdt satellite
      appears, initially empty). */
  inline constexpr ClientVersion Legion_TombOfSargeras{7, 2, 5, 24076};

  /** Legion, Shadows of Argus PTR, 7.3.0 build 24473. */
  inline constexpr ClientVersion Legion_ShadowsOfArgus_24473{7, 3, 0, 24473};

  /** Legion, Shadows of Argus PTR, 7.3.0 build 24500 (the .m2 parent-model
      and .skel satellite chunks). */
  inline constexpr ClientVersion Legion_ShadowsOfArgus_24500{7, 3, 0, 24500};

  /** Battle for Azeroth beta, 8.0.1 build 25902 (the first _fogs.wdt files
      with VFOG content). */
  inline constexpr ClientVersion BfA_Beta_25902{8, 0, 1, 25902};

  /** Battle for Azeroth beta, 8.0.1 build 26287 (the client starts loading
      the _mpv.wdt particulate-volume satellite). */
  inline constexpr ClientVersion BfA_Beta_26287{8, 0, 1, 26287};

  /** Battle for Azeroth beta, 8.0.1 build 26629 (TXID/LDV1). */
  inline constexpr ClientVersion BfA_Beta{8, 0, 1, 26629};

  /** BfA, Tides of Vengeance PTR, 8.1.0 build 27826 (RPID/GPID, WMO
      MOSI/MODI). */
  inline constexpr ClientVersion BfA_TidesOfVengeance{8, 1, 0, 27826};

  /** BfA, Tides of Vengeance, 8.1.0 build 28294 (WDT MAID and the MPHD
      FileDataID layout — the namehash removal preparation). */
  inline constexpr ClientVersion BfA_TidesOfVengeance_28294{8, 1, 0, 28294};

  /** BfA, Rise of Azshara, 8.2.0 build 30080 (WFV1/WFV2/PGD1). */
  inline constexpr ClientVersion BfA_RiseOfAzshara{8, 2, 0, 30080};

  /** BfA, Visions of N'Zoth PTR, 8.3.0 build 32044. */
  inline constexpr ClientVersion BfA_VisionsOfNzoth_32044{8, 3, 0, 32044};

  /** BfA, Visions of N'Zoth, 8.3.0 build 33775 (WMO MPVR). */
  inline constexpr ClientVersion BfA_VisionsOfNzoth_33775{8, 3, 0, 33775};

  /** BfA, Visions of N'Zoth, 8.3.7 build 35662 (M2 PFDC inline physics —
      wowdev dates the chunk to the 9.0.1.33978 alpha, but 8.3.7 shipped
      during that alpha and carries the backport; 31 item M2s in the 8.3.7
      fleet client). */
  inline constexpr ClientVersion BfA_VisionsOfNzoth_35662{8, 3, 7, 35662};

  /** Shadowlands alpha, 9.0.1 build 33978 (WFV3/EDGF/NERF/DBOC; PFDC is
      dated here by wowdev but reached late BfA too — see
      BfA_VisionsOfNzoth_35662). */
  inline constexpr ClientVersion SL_Alpha_33978{9, 0, 1, 33978};

  /** Shadowlands alpha, 9.0.1 build 34365 (DETL). */
  inline constexpr ClientVersion SL_Alpha_34365{9, 0, 1, 34365};

  /** Shadowlands beta, 9.0.1 build 34490 (_lgt.wdt MPL3 point lights). */
  inline constexpr ClientVersion SL_Beta_34490{9, 0, 1, 34490};

  /** Shadowlands, Chains of Domination PTR, 9.1.0 build 39015. */
  inline constexpr ClientVersion SL_ChainsOfDomination{9, 1, 0, 39015};

  /** Shadowlands, Eternity's End, 9.2.0 build 42423 (the MOGP split-group
      indices). */
  inline constexpr ClientVersion SL_EternitysEnd{9, 2, 0, 42423};

  /** Dragonflight alpha, 10.0.0 build 46181. */
  inline constexpr ClientVersion DF_Alpha{10, 0, 0, 46181};

  /** The War Within alpha, 11.0.0 build 54210. */
  inline constexpr ClientVersion TWW_Alpha{11, 0, 0, 54210};

  /** The War Within alpha, 11.0.0 build 54935 (_fogs.wdt version 2 and its
      VFEX extension records). */
  inline constexpr ClientVersion TWW_Alpha_54935{11, 0, 0, 54935};

  /** TWW, Undermine(d) PTR, 11.1.0 build 58221. */
  inline constexpr ClientVersion TWW_Undermined{11, 1, 0, 58221};

  /** TWW, Legacy of Arathor, 11.1.7 build 60520 (PCOL/DPIV). */
  inline constexpr ClientVersion TWW_LegacyOfArathor{11, 1, 7, 60520};
}
