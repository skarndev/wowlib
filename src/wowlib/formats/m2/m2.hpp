#pragma once

/** @file
    The M2 entity (namespace wowlib::formats::m2): a whole model with its
    satellite files baked in, versioned on the client it is laid out for. The
    assembly unifies the MD20 body (M2Data) with the external .skin LOD views
    and re-splits low-priority sequence data back out to .anim files on write
    (driven by the sequence flags). Pre-WotLK everything is one file; Legion+
    adds the chunked wrapper and .skel/.bone/.phys satellites (stage 3). */

#include <array>
#include <span>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/m2/bone/bone.hpp>
#include <wowlib/formats/m2/body/shell.hpp>
#include <wowlib/formats/m2/body/data.hpp>
#include <wowlib/formats/m2/skeleton.hpp>
#include <wowlib/formats/m2/skin/skin.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::formats::m2
{
  using body::GlobalFlags;
  using body::M2Data;
  using body::M2File;
  using body::md20_magic;
  using bone::BoneFile;
  using skin::Skin;
  using skin::skin_magic;

  /** The version-agnostic base of every M2<V> (welded as "M2").

      This empty base exists ENTIRELY for the language bindings (Python, Lua):
      it gives the per-version M2* classes a common welded supertype, and the
      module glue attaches the for_version/read/write/convert surface to it.
      It has no role in the C++ API, where you use the concrete M2<V>
      directly.

      @see https://wowdev.wiki/M2 */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("M2"),
    =welder::doc(R"(
        A whole model, abstract over the client version — the MD20 body with
        its satellite files (.skin, .anim) baked in. Construct the concrete
        version with M2.for_version(expansion), then read()/write(); the
        per-version M2* classes are subclasses. See https://wowdev.wiki/M2.)")
  ]] M2Base
  {
    bool operator==(const M2Base&) const = default;
  };

  namespace detail
  {
    // The trait primaries are EMPTY and unconstrained: the binding walk
    // completes absent<Trait>'s template argument even for the versions a
    // slot leaves inactive, so an out-of-era instantiation must be valid —
    // the members live in constrained partial specializations instead.

    /** WotLK+ assembly members: the external .skin LOD views baked in. */
    template <ClientVersion V>
    struct AssemblySkins
    {
      [[=welder::mark::exclude]]
      bool operator==(const AssemblySkins&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_per_sequence_timelines)
    struct AssemblySkins<V>
    {
      [[
        =welder::mark::no_reassign,
        =welder::doc(R"(The model's LOD views, in view order ("{model}0N.skin"
                        files, WotLK+). The source of truth for the view
                        count: write() stamps the body's num_skin_profiles
                        wire field from this vector's length.)")]]
      std::vector<Skin<V>> skins;

      [[=welder::mark::exclude]]
      bool operator==(const AssemblySkins&) const = default;
    };

    /** Legion+ assembly members: the chunk stream and the satellites it
        references. */
    template <ClientVersion V>
    struct AssemblyLegion
    {
      [[=welder::mark::exclude]]
      bool operator==(const AssemblyLegion&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_chunked_container)
    struct AssemblyLegion<V>
    {
      [[
        =welder::doc(R"(The chunked .m2 stream (Legion+): the satellite chunks
                        (FileDataIDs, extended particles, parent overrides)
                        plus preserved unknown chunks. The MD21 transport blob
                        inside is TRANSIENT: read() decodes it into `root` and
                        drops the bytes; write() re-encodes root into the
                        stream and refreshes the FileDataID chunks.)")]]
      M2File<V> chunks{};

      [[
        =welder::mark::no_reassign,
        =welder::doc("The LOD-band skins (the SFID entries beyond "
                     "num_skin_profiles), in band order.")]]
      std::vector<Skin<V>> lod_skins;

      [[
        =welder::doc("The referenced .phys file bytes (PFID), baked in "
                     "verbatim — structured physics is a follow-up "
                     "milestone. Inline physics (PFDC) stays a chunk on "
                     "the stream.")]]
      ChunkBlob phys;

      [[
        =welder::doc(R"(The shared skeleton (SKID), fully baked in — bones,
                        attachments, sequences and the parent link. Engaged
                        exactly when the chunk stream carries a skeleton
                        FileDataID; skeletons are shared between models, so
                        edit with care or write the .skel standalone.)")]]
      // through the m2:: alias, NOT the sibling detail raw — the skeleton
      // collapses to its single chunked-era instantiation
      m2::Skeleton<V> skel{};

      [[
        =welder::mark::no_reassign,
        =welder::doc("The .bone facial-pose files (BFID; the skeleton's when "
                     "skel-based), in variant order.")]]
      std::vector<BoneFile> bone_files;

      [[=welder::mark::exclude]]
      bool operator==(const AssemblyLegion&) const = default;
    };
  }

  namespace detail
  {
  /** A whole M2 model for one client version: the MD20 body plus every
      baked-in satellite. Reading resolves external sequence data (.anim) and
      the LOD views (.skin) into the entity; writing re-derives the satellite
      set — sequences with `(flags & 0x130) == 0` split back into
      "{model}AAAA-SS.anim" files, the skins into "{model}0N.skin".

      There is no byte-perfect round-trip for offset formats: writes lay data
      out canonically, and a written model re-reads equal instead.
      Instantiate through the canonicalizing m2::M2 alias, never directly.
      @tparam V the canonical client version this assembly targets.
      @see https://wowdev.wiki/M2 */
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A whole model for one client version: the MD20 body with skins and
        external sequence data baked in. A written model is canonical-layout
        and re-reads equal (no byte-perfect guarantee for offset formats).
        See https://wowdev.wiki/M2.)")
  ]] M2 : M2Base,
          slot<V, m2_per_sequence_timelines, detail::AssemblySkins<V>>,
          slot<V, m2_chunked_container, detail::AssemblyLegion<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::doc("The MD20 body — the model's root, uniform across every "
                   "era (pre-Legion it IS the file; Legion+ it is the "
                   "decoded MD21 image, whose transport blob `chunks` drops "
                   "after read).")]]
    M2Data<V> root{};

    // read()/write() weld the (FileSystem, FileKey) load/save on LUA ONLY —
    // mirroring WMO: on Python the module glue attaches the richer
    // read/write/convert/for_version surface to M2Base instead (stage 4).

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Load the model and all its satellite files from a client "
                   "filesystem, replacing this entity's contents.")]]
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key
                      [[=welder::doc("the .m2 file identity (path and/or FileDataID)")]]);

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Serialize and store the model and every satellite file "
                   "through the filesystem's project overlay; satellite names "
                   "derive from the key, which must resolve to a path.")]]
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key
                       [[=welder::doc("the .m2 file identity; must resolve to a path")]]) const;

    bool operator==(const M2&) const = default;

  private:
    // --- internal fs-I/O helpers (definitions in io.hpp; private so the
    // --- Python/Lua surface and the public C++ API stay verbs-only) --------

    /** Verify the MD20 magic and that the file's format version belongs to
        @a V's era.
        @param magic          the leading magic read from the file.
        @param format_version the version field read from the file.
        @return nothing, or FormatVersionMismatch. */
    static Result<void> check_header(std::uint32_t magic, std::uint32_t format_version);

    /** Load and parse one .skin by key into @a out (a std::vector<Skin<V>>;
        deduced so the pre-WotLK class instantiations — which the bindings
        instantiate EXPLICITLY, member declarations included — never spell
        the era-constrained Skin alias in a signature).
        @param fs   the filesystem gateway.
        @param key  the .skin identity.
        @param what the diagnostic context ("skin 2", "lod skin 1").
        @param out  the vector the parsed skin is appended to.
        @return nothing, or the contextualized error. */
    static Result<void> read_skin_into(fs::FileSystem& fs, const FileKey& key,
                                       std::string_view what, auto& out)
      requires (V >= m2_per_sequence_timelines);

    /** The read path shared by every monolithic-with-satellites era (WotLK
        through pre-Legion, and Legion-era files still shipping raw MD20):
        decode the body with name-based .anim resolution, then load the
        numbered .skin views.
        @param fs   the filesystem gateway.
        @param key  the model identity.
        @param main the model file bytes.
        @return nothing, or the first error. */
    Result<void> read_monolithic(fs::FileSystem& fs, const FileKey& key,
                                 std::span<const std::byte> main)
      requires (V >= m2_per_sequence_timelines);

    /** The Legion+ chunked read path: parse the stream, pull the skeleton in
        when the model is skel-based, then decode the MD21 image with
        FileDataID-based satellite resolution (and drop the transport blob).
        @param fs   the filesystem gateway.
        @param key  the model identity.
        @param main the model file bytes.
        @return nothing, or the first error. */
    Result<void> read_chunked(fs::FileSystem& fs, const FileKey& key,
                              std::span<const std::byte> main)
      requires (V >= m2_chunked_container);

    /** Whether the SKID reference chunk actually engages a skeleton — files
        may carry the chunk with a stored 0 meaning "none", and read and
        write MUST agree on this predicate (a mismatch would convert such a
        model into a broken skel-based one on round-trip).
        @param skeleton_fdid the SKID entries.
        @return whether a skeleton is referenced. */
    static bool skeleton_engaged(std::span<const std::uint32_t> skeleton_fdid)
    {
      return !skeleton_fdid.empty() && skeleton_fdid.front() != 0;
    }
  };
  }

  /** A whole model — the canonicalizing face of detail::M2: every client
      version maps to its range's first grid version (m2_assembly_pivots),
      so one instantiation serves e.g. both Cata and MoP. */
  template <ClientVersion V>
  using M2 = detail::M2<canonical_version(V, m2_assembly_pivots, m2_versions)>;
}

/** Per-RANGE expansion of the M2 template surface: each family's X-macro
    lists one row per REAL content permutation — X(Suffix, version) with the
    range's canonical grid version — and drives the welded aliases here plus
    the instantiation matrix in bindings/python/instantiations/. Every table
    is consteval-checked against the family's pivots (ranges_valid in
    m2::detail below), so a row set, a suffix or a pivot list cannot drift
    from the others. Extending the version list means revisiting the pivot
    lists in boundaries.hpp; the checks then dictate the rows. */
#define WOWLIB_M2_RANGES_TRACKS(X)                                                                 \
  X(VanillaToTbc, vanilla)                                                                         \
  X(WotlkPlus, wotlk)

#define WOWLIB_M2_RANGES_SEQUENCE(X)                                                               \
  X(VanillaToTbc, vanilla)                                                                         \
  X(WotlkToMop, wotlk)                                                                             \
  X(WodPlus, wod)

#define WOWLIB_M2_RANGES_BONE(X)                                                                   \
  X(Vanilla, vanilla)                                                                              \
  X(Tbc, tbc)                                                                                      \
  X(WotlkPlus, wotlk)

#define WOWLIB_M2_RANGES_CAMERA(X)                                                                 \
  X(VanillaToTbc, vanilla)                                                                         \
  X(Wotlk, wotlk)                                                                                  \
  X(CataPlus, cata)

#define WOWLIB_M2_RANGES_PARTICLE(X)                                                               \
  X(Vanilla, vanilla)                                                                              \
  X(Tbc, tbc)                                                                                      \
  X(Wotlk, wotlk)                                                                                  \
  X(CataPlus, cata)

#define WOWLIB_M2_RANGES_SKIN_SECTION(X)                                                           \
  X(Vanilla, vanilla)                                                                              \
  X(TbcPlus, tbc)

#define WOWLIB_M2_RANGES_SKIN_PROFILE(X)                                                           \
  X(Vanilla, vanilla)                                                                              \
  X(TbcToWotlk, tbc)                                                                               \
  X(CataPlus, cata)

#define WOWLIB_M2_RANGES_DATA(X)                                                                   \
  X(Vanilla, vanilla)                                                                              \
  X(Tbc, tbc)                                                                                      \
  X(Wotlk, wotlk)                                                                                  \
  X(CataToMop, cata)                                                                               \
  X(Wod, wod)                                                                                      \
  X(LegionPlus, legion)

#define WOWLIB_M2_RANGES_SKIN(X)                                                                   \
  X(Wotlk, wotlk)                                                                                  \
  X(CataPlus, cata)

#define WOWLIB_M2_RANGES_FILE(X)                                                                   \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

/** The chunk payloads (the Skel family, Exp2/Pabc/Psbc/Pgd1) and the Skeleton are
    stable across the whole chunked era: one range. */
#define WOWLIB_M2_RANGES_CHUNK_PAYLOADS(X)                                                         \
  X(LegionPlus, legion)

#define WOWLIB_M2_RANGES_ASSEMBLY(X)                                                               \
  X(Vanilla, vanilla)                                                                              \
  X(Tbc, tbc)                                                                                      \
  X(Wotlk, wotlk)                                                                                  \
  X(CataToMop, cata)                                                                               \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

// The bindings surface: welder welds a class-template instantiation through a
// namespace-scope alias, whose identifier is the target-language name; each
// family's aliases are declared in its own namespace so the per-range classes
// surface under the matching submodule (formats.m2, .body, .body.records,
// .skin, .bone). Welded NSDMI defaults convert eagerly at registration and the
// walk follows namespace-member declaration order, so within every namespace
// the records alias before the entities whose members default them, and the
// assembly comes last.
namespace wowlib::formats::m2::body::records
{
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

#define WOWLIB_M2_SHELL_RECORD_ALIAS(Suffix, version_)                                             \
  using Exp2Data##Suffix = Exp2Data<versions::version_>;                                           \
  using PabcData##Suffix = PabcData<versions::version_>;                                           \
  using PsbcData##Suffix = PsbcData<versions::version_>;                                           \
  using Pgd1Data##Suffix = Pgd1Data<versions::version_>;
  WOWLIB_M2_RANGES_CHUNK_PAYLOADS(WOWLIB_M2_SHELL_RECORD_ALIAS)
#undef WOWLIB_M2_SHELL_RECORD_ALIAS
}

namespace wowlib::formats::m2::body
{
#define WOWLIB_M2_DATA_ALIAS(Suffix, version_) using M2Data##Suffix = M2Data<versions::version_>;
  WOWLIB_M2_RANGES_DATA(WOWLIB_M2_DATA_ALIAS)
#undef WOWLIB_M2_DATA_ALIAS

#define WOWLIB_M2_FILE_ALIAS(Suffix, version_) using M2File##Suffix = M2File<versions::version_>;
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

  // the assembly last: its NSDMI defaults name M2Data/M2File/Skeleton values
#define WOWLIB_M2_ASSEMBLY_ALIAS(Suffix, version_) using M2##Suffix = M2<versions::version_>;
  WOWLIB_M2_RANGES_ASSEMBLY(WOWLIB_M2_ASSEMBLY_ALIAS)
#undef WOWLIB_M2_ASSEMBLY_ALIAS

  namespace detail
  {
    // Range-table validation: every family's rows must exactly enumerate the
    // distinct canonicals of its grid, with the suffix range_suffix derives.
#define WOWLIB_M2_RANGE_ROW(Suffix, version_)                                                      \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array m2_track_rows{WOWLIB_M2_RANGES_TRACKS(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_track_rows, m2_track_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_TRACKS drifted from m2_track_pivots");
    inline constexpr std::array m2_sequence_rows{WOWLIB_M2_RANGES_SEQUENCE(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_sequence_rows, m2_sequence_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_SEQUENCE drifted from m2_sequence_pivots");
    inline constexpr std::array m2_bone_rows{WOWLIB_M2_RANGES_BONE(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_bone_rows, m2_bone_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_BONE drifted from m2_bone_pivots");
    inline constexpr std::array m2_camera_rows{WOWLIB_M2_RANGES_CAMERA(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_camera_rows, m2_camera_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_CAMERA drifted from m2_camera_pivots");
    inline constexpr std::array m2_particle_rows{WOWLIB_M2_RANGES_PARTICLE(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_particle_rows, m2_particle_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_PARTICLE drifted from m2_particle_pivots");
    inline constexpr std::array m2_skin_section_rows{
      WOWLIB_M2_RANGES_SKIN_SECTION(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_skin_section_rows, m2_skin_section_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_SKIN_SECTION drifted from m2_skin_section_pivots");
    inline constexpr std::array m2_skin_profile_rows{
      WOWLIB_M2_RANGES_SKIN_PROFILE(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_skin_profile_rows, m2_skin_profile_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_SKIN_PROFILE drifted from m2_skin_profile_pivots");
    inline constexpr std::array m2_data_rows{WOWLIB_M2_RANGES_DATA(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_data_rows, m2_data_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_DATA drifted from m2_data_pivots");
    inline constexpr std::array m2_skin_rows{WOWLIB_M2_RANGES_SKIN(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_skin_rows, m2_skin_pivots, m2_skin_versions),
                  "WOWLIB_M2_RANGES_SKIN drifted from m2_skin_pivots");
    inline constexpr std::array m2_file_rows{WOWLIB_M2_RANGES_FILE(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_file_rows, m2_file_pivots, m2_chunked_versions),
                  "WOWLIB_M2_RANGES_FILE drifted from m2_file_pivots");
    inline constexpr std::array m2_chunk_payload_rows{
      WOWLIB_M2_RANGES_CHUNK_PAYLOADS(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_chunk_payload_rows, m2_skeleton_pivots, m2_chunked_versions),
                  "WOWLIB_M2_RANGES_CHUNK_PAYLOADS drifted from m2_skeleton_pivots");
    inline constexpr std::array m2_assembly_rows{WOWLIB_M2_RANGES_ASSEMBLY(WOWLIB_M2_RANGE_ROW)};
    static_assert(ranges_valid(m2_assembly_rows, m2_assembly_pivots, m2_versions),
                  "WOWLIB_M2_RANGES_ASSEMBLY drifted from m2_assembly_pivots");
#undef WOWLIB_M2_RANGE_ROW
  }
}

// There are NO extern-template declarations or explicit instantiations here:
// consumer TUs implicitly instantiate exactly the versions they use (the
// read/write definitions live in offset_serializer.hpp, serializer.hpp and
// io.hpp). The language bindings, which need the FULL version matrix,
// declare and expand it in their own translation units — see
// bindings/python/instantiations/m2.hpp.
