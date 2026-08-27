#pragma once

/** @file
    @brief Per-RANGE welded alias tables for the M2 template surface.

    Each family's x-macro lists one row per REAL content permutation —
    x(Suffix, version) with the range's canonical grid version — and drives
    the welded aliases here plus the instantiation matrix in m2_matrix.inl.
    Every table is consteval-checked against the family's pivots
    (rangesValid in m2::detail below), so a row set, a suffix or a pivot
    list cannot drift from the others. Extending the version list means
    revisiting the pivot lists in formats/m2/boundaries.hpp; the checks then
    dictate the rows.

    This lives in the bindings, NOT the library: C++ consumers instantiate
    the templates on demand and never need the flat per-range names — only
    welder does, because it welds a class-template instantiation through a
    namespace-scope alias whose identifier is the target-language name. */

#include <wowlib/formats/m2/m2.hpp>

#define WOWLIB_M2_RANGES_TRACKS(x)                                                                 \
  x(VanillaToTbc, Vanilla)                                                                         \
  x(WotlkPlus, Wotlk)

#define WOWLIB_M2_RANGES_SEQUENCE(x)                                                               \
  x(VanillaToTbc, Vanilla)                                                                         \
  x(WotlkToMop, Wotlk)                                                                             \
  x(WodPlus, Wod)

#define WOWLIB_M2_RANGES_BONE(x)                                                                   \
  x(Vanilla, Vanilla)                                                                              \
  x(Tbc, Tbc)                                                                                      \
  x(WotlkPlus, Wotlk)

#define WOWLIB_M2_RANGES_CAMERA(x)                                                                 \
  x(VanillaToTbc, Vanilla)                                                                         \
  x(Wotlk, Wotlk)                                                                                  \
  x(CataPlus, Cata)

#define WOWLIB_M2_RANGES_PARTICLE(x)                                                               \
  x(Vanilla, Vanilla)                                                                              \
  x(Tbc, Tbc)                                                                                      \
  x(Wotlk, Wotlk)                                                                                  \
  x(CataPlus, Cata)

#define WOWLIB_M2_RANGES_SKIN_SECTION(x)                                                           \
  x(Vanilla, Vanilla)                                                                              \
  x(TbcPlus, Tbc)

#define WOWLIB_M2_RANGES_SKIN_PROFILE(x)                                                           \
  x(Vanilla, Vanilla)                                                                              \
  x(TbcToWotlk, Tbc)                                                                               \
  x(CataPlus, Cata)

#define WOWLIB_M2_RANGES_DATA(x)                                                                   \
  x(Vanilla, Vanilla)                                                                              \
  x(Tbc, Tbc)                                                                                      \
  x(Wotlk, Wotlk)                                                                                  \
  x(CataToMop, Cata)                                                                               \
  x(Wod, Wod)                                                                                      \
  x(LegionPlus, Legion)

#define WOWLIB_M2_RANGES_SKIN(x)                                                                   \
  x(Wotlk, Wotlk)                                                                                  \
  x(CataPlus, Cata)

#define WOWLIB_M2_RANGES_FILE(x)                                                                   \
  x(Legion, Legion)                                                                                \
  x(Bfa, Bfa)                                                                                      \
  x(Shadowlands, Shadowlands)                                                                      \
  x(Dragonflight, Dragonflight)                                                                    \
  x(TheWarWithin, Tww)

/** The chunk payloads (the Skel family, Exp2/Pabc/Psbc/Pgd1) and the Skeleton are
    stable across the whole chunked era: one range. */
#define WOWLIB_M2_RANGES_CHUNK_PAYLOADS(x)                                                         \
  x(LegionPlus, Legion)

#define WOWLIB_M2_RANGES_ASSEMBLY(x)                                                               \
  x(Vanilla, Vanilla)                                                                              \
  x(Tbc, Tbc)                                                                                      \
  x(Wotlk, Wotlk)                                                                                  \
  x(CataToMop, Cata)                                                                               \
  x(Wod, Wod)                                                                                      \
  x(Legion, Legion)                                                                                \
  x(Bfa, Bfa)                                                                                      \
  x(Shadowlands, Shadowlands)                                                                      \
  x(Dragonflight, Dragonflight)                                                                    \
  x(TheWarWithin, Tww)

// The bindings surface: each family's aliases are declared in its own
// namespace so the per-range classes surface under the matching submodule
// (formats.m2, .root, .root.record, .chunked, .chunked.record, .skin, .bone).
// Welded NSDMI defaults convert eagerly at registration and the walk follows
// namespace-member declaration order, so within every namespace the records
// alias before the entities whose members default them, and the assembly
// comes last.
namespace wowlib::formats::m2::root::record
{
  // The version-agnostic BASES of the M2Track VALUE families: one
  // M2TrackFamilyBase<T> per value type, alias-welded (gen_m2.cpp) under the
  // family name so each per-value family gets its common supertype — the
  // non-template record bases weld through the namespace walk instead.
  using M2TrackC3VectorFam = M2TrackFamilyBase<C3Vector>;
  using M2TrackC4QuaternionFam = M2TrackFamilyBase<C4Quaternion>;
  using M2TrackCompQuatFam = M2TrackFamilyBase<M2CompQuat>;
  using M2TrackFloatFam = M2TrackFamilyBase<float>;
  using M2TrackFixed16Fam = M2TrackFamilyBase<fixed16>;
  using M2TrackUInt8Fam = M2TrackFamilyBase<std::uint8_t>;
  using M2TrackUInt16Fam = M2TrackFamilyBase<std::uint16_t>;
  using M2TrackSplineC3VectorFam = M2TrackFamilyBase<M2SplineKey<C3Vector>>;
  using M2TrackSplineFloatFam = M2TrackFamilyBase<M2SplineKey<float>>;

  // element/ramp types with no version axis, named once
  using M2SplineKeyC3Vector = M2SplineKey<C3Vector>;
  using M2SplineKeyFloat = M2SplineKey<float>;
  using FBlockC3Vector = FBlock<C3Vector>;
  using FBlockC2Vector = FBlock<C2Vector>;
  using FBlockFixed16 = FBlock<fixed16>;
  using FBlockUInt16 = FBlock<std::uint16_t>;
  using M2PartTrackFixed16 = M2PartTrack<fixed16>;

#define WOWLIB_M2_TRACKS_ALIAS(Suffix, version_)                                                   \
  using M2TrackC3Vector##Suffix = M2Track<C3Vector, versions::version_>;                           \
  using M2TrackC4Quaternion##Suffix = M2Track<C4Quaternion, versions::version_>;                   \
  using M2TrackCompQuat##Suffix = M2Track<M2CompQuat, versions::version_>;                         \
  using M2TrackFloat##Suffix = M2Track<float, versions::version_>;                                 \
  using M2TrackFixed16##Suffix = M2Track<fixed16, versions::version_>;                             \
  using M2TrackUInt8##Suffix = M2Track<std::uint8_t, versions::version_>;                          \
  using M2TrackUInt16##Suffix = M2Track<std::uint16_t, versions::version_>;                        \
  using M2TrackSplineC3Vector##Suffix = M2Track<M2SplineKey<C3Vector>, versions::version_>;        \
  using M2TrackSplineFloat##Suffix = M2Track<M2SplineKey<float>, versions::version_>;              \
  using M2EventTrack##Suffix = M2TrackBase<versions::version_>;                                    \
  using M2Color##Suffix = M2Color<versions::version_>;                                             \
  using M2TextureWeight##Suffix = M2TextureWeight<versions::version_>;                             \
  using M2TextureFlipbook##Suffix = M2TextureFlipbook<versions::version_>;                         \
  using M2TextureTransform##Suffix = M2TextureTransform<versions::version_>;                       \
  using M2Attachment##Suffix = M2Attachment<versions::version_>;                                   \
  using M2Event##Suffix = M2Event<versions::version_>;                                             \
  using M2Light##Suffix = M2Light<versions::version_>;                                             \
  using M2Ribbon##Suffix = M2Ribbon<versions::version_>;
  WOWLIB_M2_RANGES_TRACKS(WOWLIB_M2_TRACKS_ALIAS)
#undef WOWLIB_M2_TRACKS_ALIAS

#define WOWLIB_M2_SEQUENCE_ALIAS(Suffix, version_)                                                 \
  using M2Sequence##Suffix = M2Sequence<versions::version_>;
  WOWLIB_M2_RANGES_SEQUENCE(WOWLIB_M2_SEQUENCE_ALIAS)
#undef WOWLIB_M2_SEQUENCE_ALIAS

#define WOWLIB_M2_BONE_ALIAS(Suffix, version_)                                                     \
  using M2CompBone##Suffix = M2CompBone<versions::version_>;
  WOWLIB_M2_RANGES_BONE(WOWLIB_M2_BONE_ALIAS)
#undef WOWLIB_M2_BONE_ALIAS

#define WOWLIB_M2_CAMERA_ALIAS(Suffix, version_)                                                   \
  using M2Camera##Suffix = M2Camera<versions::version_>;
  WOWLIB_M2_RANGES_CAMERA(WOWLIB_M2_CAMERA_ALIAS)
#undef WOWLIB_M2_CAMERA_ALIAS

#define WOWLIB_M2_PARTICLE_ALIAS(Suffix, version_)                                                 \
  using M2Particle##Suffix = M2Particle<versions::version_>;
  WOWLIB_M2_RANGES_PARTICLE(WOWLIB_M2_PARTICLE_ALIAS)
#undef WOWLIB_M2_PARTICLE_ALIAS
}

// Exp2Data/PabcData/PsbcData/Pgd1Data are non-templated (version-independent
// content, WotLK+-fixed layout) — one welded class each, no per-range aliases.

namespace wowlib::formats::m2::root
{
#define WOWLIB_M2_DATA_ALIAS(Suffix, version_) using M2Root##Suffix = M2Root<versions::version_>;
  WOWLIB_M2_RANGES_DATA(WOWLIB_M2_DATA_ALIAS)
#undef WOWLIB_M2_DATA_ALIAS
}

namespace wowlib::formats::m2::chunked
{
#define WOWLIB_M2_FILE_ALIAS(Suffix, version_)                                                     \
  using M2ChunkedFile##Suffix = M2ChunkedFile<versions::version_>;
  WOWLIB_M2_RANGES_FILE(WOWLIB_M2_FILE_ALIAS)
#undef WOWLIB_M2_FILE_ALIAS
}

namespace wowlib::formats::m2::skin
{
#define WOWLIB_M2_SKIN_SECTION_ALIAS(Suffix, version_)                                             \
  using M2SkinSection##Suffix = M2SkinSection<versions::version_>;
  WOWLIB_M2_RANGES_SKIN_SECTION(WOWLIB_M2_SKIN_SECTION_ALIAS)
#undef WOWLIB_M2_SKIN_SECTION_ALIAS

#define WOWLIB_M2_SKIN_PROFILE_ALIAS(Suffix, version_)                                             \
  using M2SkinProfile##Suffix = M2SkinProfile<versions::version_>;
  WOWLIB_M2_RANGES_SKIN_PROFILE(WOWLIB_M2_SKIN_PROFILE_ALIAS)
#undef WOWLIB_M2_SKIN_PROFILE_ALIAS

#define WOWLIB_M2_SKIN_ALIAS(Suffix, version_) using Skin##Suffix = Skin<versions::version_>;
  WOWLIB_M2_RANGES_SKIN(WOWLIB_M2_SKIN_ALIAS)
#undef WOWLIB_M2_SKIN_ALIAS
}

namespace wowlib::formats::m2
{
#define WOWLIB_M2_SKELETON_ALIAS(Suffix, version_)                                                 \
  using SkelHeader##Suffix = SkelHeader<versions::version_>;                                       \
  using SkelSequences##Suffix = SkelSequences<versions::version_>;                                 \
  using SkelBones##Suffix = SkelBones<versions::version_>;                                         \
  using SkelAttachments##Suffix = SkelAttachments<versions::version_>;                             \
  using Skeleton##Suffix = Skeleton<versions::version_>;
  WOWLIB_M2_RANGES_CHUNK_PAYLOADS(WOWLIB_M2_SKELETON_ALIAS)
#undef WOWLIB_M2_SKELETON_ALIAS

  // the assembly last: its NSDMI defaults name M2Root/M2ChunkedFile/Skeleton values
#define WOWLIB_M2_ASSEMBLY_ALIAS(Suffix, version_) using M2##Suffix = M2<versions::version_>;
  WOWLIB_M2_RANGES_ASSEMBLY(WOWLIB_M2_ASSEMBLY_ALIAS)
#undef WOWLIB_M2_ASSEMBLY_ALIAS

  namespace detail
  {
    // Range-table validation: every family's rows must exactly enumerate the
    // distinct canonicals of its grid, with the suffix rangeSuffix derives.
#define WOWLIB_M2_RANGE_ROW(Suffix, version_)                                                      \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array M2TrackRows{WOWLIB_M2_RANGES_TRACKS(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2TrackRows, M2TrackPivots, M2Versions),
                  "WOWLIB_M2_RANGES_TRACKS drifted from m2_track_pivots");
    inline constexpr std::array M2SequenceRows{WOWLIB_M2_RANGES_SEQUENCE(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2SequenceRows, M2SequencePivots, M2Versions),
                  "WOWLIB_M2_RANGES_SEQUENCE drifted from m2_sequence_pivots");
    inline constexpr std::array M2BoneRows{WOWLIB_M2_RANGES_BONE(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2BoneRows, M2BonePivots, M2Versions),
                  "WOWLIB_M2_RANGES_BONE drifted from m2_bone_pivots");
    inline constexpr std::array M2CameraRows{WOWLIB_M2_RANGES_CAMERA(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2CameraRows, M2CameraPivots, M2Versions),
                  "WOWLIB_M2_RANGES_CAMERA drifted from m2_camera_pivots");
    inline constexpr std::array M2ParticleRows{WOWLIB_M2_RANGES_PARTICLE(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2ParticleRows, M2ParticlePivots, M2Versions),
                  "WOWLIB_M2_RANGES_PARTICLE drifted from m2_particle_pivots");
    inline constexpr std::array M2SkinSectionRows{
      WOWLIB_M2_RANGES_SKIN_SECTION(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2SkinSectionRows, M2SkinSectionPivots, M2Versions),
                  "WOWLIB_M2_RANGES_SKIN_SECTION drifted from m2_skin_section_pivots");
    inline constexpr std::array M2SkinProfileRows{
      WOWLIB_M2_RANGES_SKIN_PROFILE(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2SkinProfileRows, M2SkinProfilePivots, M2Versions),
                  "WOWLIB_M2_RANGES_SKIN_PROFILE drifted from m2_skin_profile_pivots");
    inline constexpr std::array M2DataRows{WOWLIB_M2_RANGES_DATA(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2DataRows, M2DataPivots, M2Versions),
                  "WOWLIB_M2_RANGES_DATA drifted from m2_data_pivots");
    inline constexpr std::array M2SkinRows{WOWLIB_M2_RANGES_SKIN(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2SkinRows, M2SkinPivots, M2SkinVersions),
                  "WOWLIB_M2_RANGES_SKIN drifted from m2_skin_pivots");
    inline constexpr std::array M2FileRows{WOWLIB_M2_RANGES_FILE(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2FileRows, M2FilePivots, M2ChunkedVersions),
                  "WOWLIB_M2_RANGES_FILE drifted from m2_file_pivots");
    inline constexpr std::array M2ChunkPayloadRows{
      WOWLIB_M2_RANGES_CHUNK_PAYLOADS(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2ChunkPayloadRows, M2ChunkPayloadPivots, M2ChunkedVersions),
                  "WOWLIB_M2_RANGES_CHUNK_PAYLOADS drifted from m2_chunk_payload_pivots");
    inline constexpr std::array M2AssemblyRows{WOWLIB_M2_RANGES_ASSEMBLY(WOWLIB_M2_RANGE_ROW)};
    static_assert(rangesValid(M2AssemblyRows, M2AssemblyPivots, M2Versions),
                  "WOWLIB_M2_RANGES_ASSEMBLY drifted from m2_assembly_pivots");
#undef WOWLIB_M2_RANGE_ROW
  }
}
