#pragma once

/** @file
    The M2 entity (namespace wowlib::formats::m2): a whole model with its
    satellite files baked in, versioned on the client it is laid out for. The
    assembly unifies the MD20 body (M2Data) with the external .skin LOD views
    and re-splits low-priority sequence data back out to .anim files on write
    (driven by the sequence flags). Pre-WotLK everything is one file; Legion+
    adds the chunked wrapper and .skel/.bone/.phys satellites (stage 3). */

#include <array>
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

  /** The versions M2 is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. Kept in sync with the
      alias/instantiation X-macro below (checked by static_assert). */
  inline constexpr std::array m2_versions{
    versions::vanilla, versions::tbc,         versions::wotlk,
    versions::cata,    versions::mop,         versions::wod,
    versions::legion,  versions::bfa,         versions::shadowlands,
    versions::dragonflight, versions::tww};

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
                        files, WotLK+); num_skin_profiles on the body must
                        match — write() validates.)")]]
      std::vector<Skin<V>> skins;

      [[=welder::mark::exclude]]
      bool operator==(const AssemblySkins&) const = default;
    };

    /** Legion+ assembly members: the chunked shell and the satellites it
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
        =welder::doc(R"(The chunked .m2 shell: satellite chunks (FileDataIDs,
                        extended particles, parent overrides) plus preserved
                        unknown chunks. Its MD21 blob is the on-disk image;
                        `data` is the decoded form — write() re-encodes data
                        into it and refreshes the FileDataID chunks.)")]]
      M2File<V> shell{};

      [[
        =welder::mark::no_reassign,
        =welder::doc("The LOD-band skins (the SFID entries beyond "
                     "num_skin_profiles), in band order.")]]
      std::vector<Skin<V>> lod_skins;

      [[
        =welder::doc("The referenced .phys file bytes (PFID), baked in "
                     "verbatim — structured physics is a follow-up "
                     "milestone. Inline physics (PFDC) stays on the shell.")]]
      ChunkBlob phys;

      [[
        =welder::doc(R"(The shared skeleton (SKID), fully baked in — bones,
                        attachments, sequences and the parent link. Engaged
                        exactly when the shell carries a skeleton FileDataID;
                        skeletons are shared between models, so edit with
                        care or write the .skel standalone.)")]]
      Skeleton<V> skel{};

      [[
        =welder::mark::no_reassign,
        =welder::doc("The .bone facial-pose files (BFID; the skeleton's when "
                     "skel-based), in variant order.")]]
      std::vector<BoneFile> bone_files;

      [[=welder::mark::exclude]]
      bool operator==(const AssemblyLegion&) const = default;
    };
  }

  /** A whole M2 model for one client version: the MD20 body plus every
      baked-in satellite. Reading resolves external sequence data (.anim) and
      the LOD views (.skin) into the entity; writing re-derives the satellite
      set — sequences with `(flags & 0x130) == 0` split back into
      "{model}AAAA-SS.anim" files, the skins into "{model}0N.skin".

      There is no byte-perfect round-trip for offset formats: writes lay data
      out canonically, and a written model re-reads equal instead.
      @tparam V the client version this assembly targets.
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

    [[=welder::doc("The MD20 body.")]]
    M2Data<V> data{};

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
  };
}

/** Per-version expansion of the M2 template surface. X(Suffix, version) is
    invoked once per targeted release, in release order — the single spot that
    couples m2::m2_versions, the welded aliases and the explicit
    instantiations. Extending the version list means adding one row here. */
#define WOWLIB_M2_FOR_EACH_VERSION(X)                                                              \
  X(Vanilla, vanilla)                                                                              \
  X(Tbc, tbc)                                                                                      \
  X(Wotlk, wotlk)                                                                                  \
  X(Cata, cata)                                                                                    \
  X(Mop, mop)                                                                                      \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

/** The subset with external .skin files (Skin<V> exists WotLK+ only). */
#define WOWLIB_M2_FOR_EACH_SKIN_VERSION(X)                                                         \
  X(Wotlk, wotlk)                                                                                  \
  X(Cata, cata)                                                                                    \
  X(Mop, mop)                                                                                      \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

/** The subset with the chunked shell (M2File<V> exists Legion+ only). */
#define WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(X)                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

// The bindings surface: welder welds a class-template instantiation through a
// namespace-scope alias, whose identifier is the target-language name; each
// family's aliases are declared in its own namespace so the per-version
// classes surface under the matching submodule (formats.m2, .body,
// .body.records, .skin, .bone). Welded NSDMI defaults convert eagerly at
// registration and the walk follows namespace-member declaration order, so
// within every namespace the records alias before the entities whose members
// default them, and the assembly comes last.
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

#define WOWLIB_M2_RECORD_ALIAS(Suffix, version_)                                                   \
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
  using M2Sequence##Suffix = M2Sequence<versions::version_>;                                       \
  using M2CompBone##Suffix = M2CompBone<versions::version_>;                                       \
  using M2Color##Suffix = M2Color<versions::version_>;                                             \
  using M2TextureWeight##Suffix = M2TextureWeight<versions::version_>;                             \
  using M2TextureFlipbook##Suffix = M2TextureFlipbook<versions::version_>;                         \
  using M2TextureTransform##Suffix = M2TextureTransform<versions::version_>;                       \
  using M2Attachment##Suffix = M2Attachment<versions::version_>;                                   \
  using M2Event##Suffix = M2Event<versions::version_>;                                             \
  using M2Light##Suffix = M2Light<versions::version_>;                                             \
  using M2Camera##Suffix = M2Camera<versions::version_>;                                           \
  using M2Ribbon##Suffix = M2Ribbon<versions::version_>;                                           \
  using M2Particle##Suffix = M2Particle<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_RECORD_ALIAS)
#undef WOWLIB_M2_RECORD_ALIAS

#define WOWLIB_M2_SHELL_RECORD_ALIAS(Suffix, version_)                                             \
  using Exp2Data##Suffix = Exp2Data<versions::version_>;                                           \
  using PabcData##Suffix = PabcData<versions::version_>;                                           \
  using PsbcData##Suffix = PsbcData<versions::version_>;                                           \
  using Pgd1Data##Suffix = Pgd1Data<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_SHELL_RECORD_ALIAS)
#undef WOWLIB_M2_SHELL_RECORD_ALIAS
}

namespace wowlib::formats::m2::body
{
#define WOWLIB_M2_DATA_ALIAS(Suffix, version_) using M2Data##Suffix = M2Data<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_DATA_ALIAS)
#undef WOWLIB_M2_DATA_ALIAS

#define WOWLIB_M2_FILE_ALIAS(Suffix, version_) using M2File##Suffix = M2File<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_FILE_ALIAS)
#undef WOWLIB_M2_FILE_ALIAS
}

namespace wowlib::formats::m2::skin
{
#define WOWLIB_M2_SKIN_RECORD_ALIAS(Suffix, version_)                                              \
  using M2SkinSection##Suffix = M2SkinSection<versions::version_>;                                 \
  using M2SkinProfile##Suffix = M2SkinProfile<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_SKIN_RECORD_ALIAS)
#undef WOWLIB_M2_SKIN_RECORD_ALIAS

#define WOWLIB_M2_SKIN_ALIAS(Suffix, version_) using Skin##Suffix = Skin<versions::version_>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_SKIN_ALIAS)
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
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_SKELETON_ALIAS)
#undef WOWLIB_M2_SKELETON_ALIAS

  // the assembly last: its NSDMI defaults name M2Data/M2File/Skeleton values
#define WOWLIB_M2_ASSEMBLY_ALIAS(Suffix, version_) using M2##Suffix = M2<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_ASSEMBLY_ALIAS)
#undef WOWLIB_M2_ASSEMBLY_ALIAS

  namespace detail
  {
    /** X-macro coverage check: every alias row must be an m2_versions entry
        and vice versa. */
    consteval bool m2_macro_covers_versions()
    {
      std::size_t rows = 0;
#define WOWLIB_M2_COUNT_ROW(Suffix, version_)                                                      \
  {                                                                                                \
    ++rows;                                                                                        \
    bool found = false;                                                                            \
    for (const ClientVersion& v : m2_versions)                                                     \
      found = found || v == versions::version_;                                                    \
    if (!found)                                                                                    \
      return false;                                                                                \
  }
      WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_COUNT_ROW)
#undef WOWLIB_M2_COUNT_ROW
      return rows == m2_versions.size();
    }
    static_assert(m2_macro_covers_versions(),
                  "WOWLIB_M2_FOR_EACH_VERSION must list exactly the m2_versions entries");
  }

}

// There are NO extern-template declarations or explicit instantiations here:
// consumer TUs implicitly instantiate exactly the versions they use (the
// read/write definitions live in offset_serializer.hpp, serializer.hpp and
// io.hpp). The language bindings, which need the FULL version matrix,
// declare and expand it in their own translation units — see
// bindings/python/instantiations/m2.hpp.
