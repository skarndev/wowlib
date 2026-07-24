#pragma once

/** @file
    The M2 version vocabulary: the layout pivots record/entity partial
    specializations key on, and the MD20 format_version written for each
    targeted client. The M2 wire version (256–274) moves with the client but
    is stored per file — reading trusts the requested entity version's layout
    and cross-checks the file's value. */

#include <array>
#include <cstdint>
#include <utility>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_range.hpp>

namespace wowlib::formats::m2
{
  /** TBC (v260+): bone rotations become compressed M2CompQuat (vanilla stored
      raw C4Quaternion), M2CompBone gains the boneNameCRC field, and the
      particle header packs blendingType/emitterType into bytes next to the
      new particleColorIndex (late-TBC v262 on the wire; TBC's last minor
      2.4.3 writes v263, so the whole tbc target is past it). */
  inline constexpr ClientVersion m2_compressed_bones{2, 0, 0, 0};

  /** WotLK (v264): every M2Track nests one timestamp/value array per
      sequence (the vanilla single timeline with interpolation ranges is
      gone), M2Sequence stores a duration instead of global start/end
      timestamps, skin profiles move out to .skin files and low-priority
      sequences to .anim files, particles gain the FBlock color/scale tracks
      and the four spin fields, ribbons their priority-plane tail. */
  inline constexpr ClientVersion m2_per_sequence_timelines{3, 0, 0, 0};

  /** Cata (v272): particles turn multi-textured (492-byte record: texture id
      bitfield, multiTexScale replacing particleType/headOrTail, the
      multiTexScroll tail) and cameras trade the static diagonal FOV for a
      spline track. */
  inline constexpr ClientVersion m2_multitex_particles{4, 0, 0, 0};

  /** WoD (6.0.1): M2Sequence's u32 blendTime splits into the
      blendTimeIn/blendTimeOut pair. */
  inline constexpr ClientVersion m2_split_blend_times{6, 0, 0, 0};

  /** Legion (7.0.1.20740): the on-disk .m2 becomes a chunked file — the MD20
      image moves into the MD21 chunk (offsets stay relative to the image)
      joined by the companion chunks (PFID/SFID/AFID/…, forward fourccs). */
  inline constexpr ClientVersion m2_chunked_container{7, 0, 1, 20740};

  /** BfA (8.0.1): the chunked container is universal — Legion clients still
      served leftover raw MD20 models, but from 8.0 on none exist, so BfA+
      reads treat a bare MD20 magic as a version mismatch instead of falling
      back to the monolithic path. */
  inline constexpr ClientVersion m2_chunked_only{8, 0, 1, 0};

  /** The MD20 format_version wowlib writes for @a v — the value the client
      era itself exports (wowdev.wiki/M2, the Versions section): vanilla 256, TBC 263,
      WotLK 264, Cata through WoD 272, Legion+ 274.
      @param v the targeted client version.
      @return the wire version number. */
  consteval std::uint32_t m2_wire_version(ClientVersion v)
  {
    if (v >= m2_chunked_container)
      return 274;
    if (v >= m2_multitex_particles)
      return 272;
    if (v >= m2_per_sequence_timelines)
      return 264;
    if (v >= m2_compressed_bones)
      return 263;
    return 256;
  }

  /** The inclusive MD20 version range a client era's files may carry —
      reading accepts the whole era (a 2.4.3 client still ships v260 models),
      writing always emits m2_wire_version().
      @param v the targeted client version.
      @return {era minimum, era maximum}. */
  consteval std::pair<std::uint32_t, std::uint32_t> m2_wire_version_range(ClientVersion v)
  {
    if (v >= m2_chunked_container)
      return {264, 274};
    if (v >= m2_multitex_particles)
      return {265, 273};
    if (v >= m2_per_sequence_timelines)
      return {264, 264};
    if (v >= m2_compressed_bones)
      return {258, 263};
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
  // consteval-checked against these lists (ranges_valid), so neither side
  // can drift.

  /** The versions M2 is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. */
  inline constexpr std::array m2_versions{
    versions::vanilla, versions::tbc,         versions::wotlk,
    versions::cata,    versions::mop,         versions::wod,
    versions::legion,  versions::bfa,         versions::shadowlands,
    versions::dragonflight, versions::tww};

  /** The era subset .skin files exist for (WotLK+). */
  inline constexpr std::array m2_skin_versions{
    versions::wotlk, versions::cata,        versions::mop,
    versions::wod,   versions::legion,      versions::bfa,
    versions::shadowlands, versions::dragonflight, versions::tww};

  /** The era subset the chunked container exists for (Legion+). */
  inline constexpr std::array m2_chunked_versions{
    versions::legion, versions::bfa, versions::shadowlands,
    versions::dragonflight, versions::tww};

  /** Track-shaped records (M2Track, M2TrackBase, and every record whose only
      version axis is the tracks it embeds: colors, weights, transforms,
      flipbooks, attachments, events, lights, ribbons — the ribbon's WotLK
      trailing fields share the same pivot). */
  inline constexpr std::array m2_track_pivots{m2_per_sequence_timelines};

  /** M2Sequence: timestamps→duration at WotLK, blend-time split at WoD. */
  inline constexpr std::array m2_sequence_pivots{m2_per_sequence_timelines,
                                                m2_split_blend_times};

  /** M2CompBone: raw→compressed quaternions + name CRC at TBC, per-sequence
      track timelines at WotLK. */
  inline constexpr std::array m2_bone_pivots{m2_compressed_bones,
                                             m2_per_sequence_timelines};

  /** M2Camera: per-sequence tracks at WotLK, static FoV→spline at Cata. */
  inline constexpr std::array m2_camera_pivots{m2_per_sequence_timelines,
                                               m2_multitex_particles};

  /** M2Particle: byte-packed blending/emitter types at TBC, FBlock ramps and
      spins at WotLK, the multi-texture 492-byte layout at Cata. */
  inline constexpr std::array m2_particle_pivots{m2_compressed_bones,
                                                 m2_per_sequence_timelines,
                                                 m2_multitex_particles};

  /** M2SkinSection: the sort center/radius tail at TBC. */
  inline constexpr std::array m2_skin_section_pivots{m2_compressed_bones};

  /** M2SkinProfile: the section layout at TBC, shadow batches at Cata. */
  inline constexpr std::array m2_skin_profile_pivots{m2_compressed_bones,
                                                     m2_multitex_particles};

  /** The Skin entity (.skin files, WotLK+): its existence boundary plus the
      profile's Cata shadow batches. Canonicalized over m2_skin_versions. */
  inline constexpr std::array m2_skin_pivots{m2_per_sequence_timelines,
                                             m2_multitex_particles};

  /** M2Data (the MD20 body): the union of every record pivot, its own trait
      slots (TBC combos, WotLK external skins) and the wire-version steps
      (263/264/272/274). */
  inline constexpr std::array m2_data_pivots{
    m2_compressed_bones, m2_per_sequence_timelines, m2_multitex_particles,
    m2_split_blend_times, m2_chunked_container};

  /** M2File (the chunked stream): every chunk-introduction build — each
      documented since() value, so the active chunk set is constant within a
      range. Canonicalized over m2_chunked_versions. */
  inline constexpr std::array m2_file_pivots{
    m2_chunked_container,
    ClientVersion{7, 3, 0, 24500},   // EXP2/PABC/PADC/PSBC/PEDC/SKID
    ClientVersion{8, 0, 1, 26629},   // TXID/LDV1
    ClientVersion{8, 1, 0, 27826},   // RPID/GPID
    ClientVersion{8, 2, 0, 30080},   // WFV1/WFV2/PGD1
    ClientVersion{9, 0, 1, 33978},   // WFV3/PFDC/EDGF/NERF/DBOC
    ClientVersion{9, 0, 1, 34365},   // DETL
    ClientVersion{10, 0, 0, 0},      // AFRA
    ClientVersion{11, 1, 7, 60520}}; // PCOL/DPIV

  /** Skeleton and its chunk payloads plus the shell payload records: stable
      across the whole chunked era — no pivots, one instantiation. */
  inline constexpr std::array<ClientVersion, 0> m2_skeleton_pivots{};

  /** The M2 assembly: the union of the body, skin and stream pivots plus the
      bare-MD20 read gate (m2_chunked_only). */
  inline constexpr std::array m2_assembly_pivots{
    m2_compressed_bones, m2_per_sequence_timelines, m2_multitex_particles,
    m2_split_blend_times, m2_chunked_container, m2_chunked_only,
    ClientVersion{7, 3, 0, 24500},  ClientVersion{8, 0, 1, 26629},
    ClientVersion{8, 1, 0, 27826},  ClientVersion{8, 2, 0, 30080},
    ClientVersion{9, 0, 1, 33978},  ClientVersion{9, 0, 1, 34365},
    ClientVersion{10, 0, 0, 0},     ClientVersion{11, 1, 7, 60520}};
}
