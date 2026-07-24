#pragma once

/** @file
    The M2 version vocabulary: the layout pivots record/entity partial
    specializations key on, and the MD20 format_version written for each
    targeted client. The M2 wire version (256–274) moves with the client but
    is stored per file — reading trusts the requested entity version's layout
    and cross-checks the file's value. */

#include <cstdint>
#include <utility>

#include <wowlib/core/client_version.hpp>

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
}
