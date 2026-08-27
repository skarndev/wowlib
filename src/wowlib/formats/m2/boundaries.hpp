#pragma once

/** @file
    The M2 version vocabulary: the layout pivots record/entity partial
    specializations key on, and the MD20 formatVersion written for each
    targeted client. The M2 format version (256–274) moves with the client but
    is stored per file — reading trusts the requested entity version's layout
    and cross-checks the file's value. */

#include <array>
#include <cstdint>
#include <utility>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

namespace wowlib::formats::m2 {
  /** TBC (v260+): bone rotations become compressed M2CompQuat (vanilla stored
      raw C4Quaternion), M2CompBone gains the boneNameCRC field, and the
      particle header packs blendingType/emitterType into bytes next to the
      new particleColorIndex (late-TBC v262 in the format; TBC's last minor
      2.4.3 writes v263, so the whole tbc target is past it). */
  inline constexpr ClientVersion M2CompressedBones = builds::TBC;

  /** WotLK (v264): every M2Track nests one timestamp/value array per
      sequence (the vanilla single timeline with interpolation ranges is
      gone), M2Sequence stores a duration instead of global start/end
      timestamps, skin profiles move out to .skin files and low-priority
      sequences to .anim files, particles gain the FBlock color/scale tracks
      and the four spin fields, ribbons their priority-plane tail. */
  inline constexpr ClientVersion M2PerSequenceTimelines = builds::WotLK;

  /** cata (v272): particles turn multi-textured (492-byte record: texture id
      bitfield, multiTexScale replacing particleType/headOrTail, the
      multiTexScroll tail) and cameras trade the static diagonal FOV for a
      spline track. */
  inline constexpr ClientVersion M2MultitexParticles = builds::Cata;

  /** WoD (6.0.1): M2Sequence's u32 blendTime splits into the
      blendTimeIn/blendTimeOut pair. */
  inline constexpr ClientVersion M2SplitBlendTimes = builds::WoD;

  /** legion (7.0.1.20740): the on-disk .m2 becomes a chunked file — the MD20
      image moves into the MD21 chunk (offsets stay relative to the image)
      joined by the companion chunks (PFID/SFID/AFID/…, forward fourccs). */
  inline constexpr ClientVersion M2ChunkedContainer = builds::Legion_Alpha;

  /** BfA (8.0.1): the chunked container is universal — Legion clients still
      served leftover raw MD20 models, but from 8.0 on none exist, so BfA+
      reads treat a bare MD20 magic as a version mismatch instead of falling
      back to the monolithic path. */
  inline constexpr ClientVersion M2ChunkedOnly = builds::BfA;

  /** The MD20 formatVersion wowlib writes for @a v — the value the client
      era itself exports (wowdev.wiki/M2, the Versions section): vanilla 256, TBC 263,
      WotLK 264, Cata through WoD 272, Legion+ 274. Matches the entity's
      formatVersion member.
      @param v the targeted client version.
      @return the on-disk format version number. */
  consteval std::uint32_t m2FormatVersion(ClientVersion v) {
    if (v >= M2ChunkedContainer) return 274;
    if (v >= M2MultitexParticles) return 272;
    if (v >= M2PerSequenceTimelines) return 264;
    if (v >= M2CompressedBones) return 263;
    return 256;
  }

  /** The inclusive MD20 version range a client era's files may carry —
      reading accepts the whole era (a 2.4.3 client still ships v260 models),
      writing always emits m2FormatVersion().
      @param v the targeted client version.
      @return {era minimum, era maximum}. */
  consteval std::pair<std::uint32_t, std::uint32_t> m2FormatVersionRange(ClientVersion v) {
    if (v >= M2ChunkedContainer) return {264, 274};
    if (v >= M2MultitexParticles) return {265, 273};
    if (v >= M2PerSequenceTimelines) return {264, 264};
    if (v >= M2CompressedBones) return {258, 263};
    return {256, 257};
  }

  // --- version grids and per-family canonicalization pivots -----------------
  //
  // Every versioned M2 family instantiates only for its REAL content
  // permutations: the public name is a canonicalizing alias over the nested
  // detail:: template (see formats/common/version_range.hpp), flooring the
  // requested version over the family's pivots below. A pivot that does not
  // separate two grid versions is a harmless no-op, so the lists are
  // generous — they name every documented change, not just the ones the
  // current grid straddles. The welded range tables in m2.hpp are
  // consteval-checked against these lists (rangesValid), so neither side
  // can drift.

  /** The versions M2 is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. */
  inline constexpr std::array M2Versions{
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

  /** The era subset .skin files exist for (WotLK+). */
  inline constexpr std::array M2SkinVersions{
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

  /** The era subset the chunked container exists for (Legion+). */
  inline constexpr std::array M2ChunkedVersions{
    versions::Legion,
    versions::Bfa,
    versions::Shadowlands,
    versions::Dragonflight,
    versions::Tww
  };

  /** Track-shaped records (M2Track, M2TrackBase, and every record whose only
      version axis is the tracks it embeds: colors, weights, transforms,
      flipbooks, attachments, events, lights, ribbons — the ribbon's WotLK
      trailing fields share the same pivot). */
  inline constexpr std::array M2TrackPivots{M2PerSequenceTimelines};

  /** M2Sequence: timestamps→duration at WotLK, blend-time split at WoD. */
  inline constexpr std::array M2SequencePivots{M2PerSequenceTimelines, M2SplitBlendTimes};

  /** M2CompBone: raw→compressed quaternions + name CRC at TBC, per-sequence
      track timelines at WotLK. */
  inline constexpr std::array M2BonePivots{M2CompressedBones, M2PerSequenceTimelines};

  /** M2Camera: per-sequence tracks at WotLK, static FoV→spline at Cata. */
  inline constexpr std::array M2CameraPivots{M2PerSequenceTimelines, M2MultitexParticles};

  /** M2Particle: byte-packed blending/emitter types at TBC, FBlock ramps and
      spins at WotLK, the multi-texture 492-byte layout at Cata. */
  inline constexpr std::array M2ParticlePivots{M2CompressedBones, M2PerSequenceTimelines, M2MultitexParticles};

  /** M2SkinSection: the sort center/radius tail at TBC. */
  inline constexpr std::array M2SkinSectionPivots{M2CompressedBones};

  /** M2SkinProfile: the section layout at TBC, shadow batches at Cata. */
  inline constexpr std::array M2SkinProfilePivots{M2CompressedBones, M2MultitexParticles};

  /** The Skin entity (.skin files, WotLK+): its existence boundary plus the
      profile's Cata shadow batches. Canonicalized over M2SkinVersions. */
  inline constexpr std::array M2SkinPivots{M2PerSequenceTimelines, M2MultitexParticles};

  /** M2Root (the MD20 body): the union of every record pivot, its own trait
      slots (TBC combos, WotLK external skins) and the format-version steps
      (263/264/272/274). */
  inline constexpr std::array M2DataPivots{
    M2CompressedBones,
    M2PerSequenceTimelines,
    M2MultitexParticles,
    M2SplitBlendTimes,
    M2ChunkedContainer
  };

  /** M2ChunkedFile (the chunked stream): every chunk-introduction build — each
      documented since() value, so the active chunk set is constant within a
      range. Canonicalized over M2ChunkedVersions. */
  inline constexpr std::array M2FilePivots{
    M2ChunkedContainer,
    builds::Legion_ShadowsOfArgus_24500,
    // EXP2/PABC/PADC/PSBC/PEDC/SKID
    builds::BfA_Beta,
    // TXID/LDV1
    builds::BfA_TidesOfVengeance,
    // RPID/GPID
    builds::BfA_RiseOfAzshara,
    // WFV1/WFV2/PGD1
    builds::BfA_VisionsOfNzoth_35662,
    // PFDC (9.0.1-alpha chunk backported)
    builds::SL_Alpha_33978,
    // WFV3/EDGF/NERF/DBOC
    builds::SL_Alpha_34365,
    // DETL
    builds::DF,
    // AFRA
    builds::TWW_LegacyOfArathor
  }; // PCOL/DPIV

  /** Skeleton and its chunk payloads plus the shell payload records: stable
      across the whole chunked era — no pivots, one instantiation. */
  inline constexpr std::array<ClientVersion, 0> M2ChunkPayloadPivots{};

  /** The M2 assembly: the union of the body, skin and stream pivots plus the
      bare-MD20 read gate (M2ChunkedOnly). */
  inline constexpr std::array M2AssemblyPivots{
    M2CompressedBones,
    M2PerSequenceTimelines,
    M2MultitexParticles,
    M2SplitBlendTimes,
    M2ChunkedContainer,
    M2ChunkedOnly,
    builds::Legion_ShadowsOfArgus_24500,
    builds::BfA_Beta,
    builds::BfA_TidesOfVengeance,
    builds::BfA_RiseOfAzshara,
    builds::BfA_VisionsOfNzoth_35662,
    builds::SL_Alpha_33978,
    builds::SL_Alpha_34365,
    builds::DF,
    builds::TWW_LegacyOfArathor
  };
}
