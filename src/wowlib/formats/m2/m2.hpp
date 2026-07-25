#pragma once

/** @file
    The M2 entity (namespace wowlib::formats::m2): a whole model with its
    satellite files baked in, versioned on the client it is laid out for. The
    assembly unifies the MD20 body (M2Root) with the external .skin LOD views
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
#include <wowlib/formats/m2/chunked/chunked.hpp>
#include <wowlib/formats/m2/root/root.hpp>
#include <wowlib/formats/m2/skeleton.hpp>
#include <wowlib/formats/m2/skin/skin.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::formats::m2
{
  using root::GlobalFlags;
  using root::md20_magic;
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
      M2ChunkedFile<V> chunks{};

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
    M2Root<V> root{};

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

// There are NO welded per-range alias tables, extern-template declarations or
// explicit instantiations here: C++ consumer TUs implicitly instantiate
// exactly the versions they use (the read/write definitions live in
// offset_serializer.hpp, serializer.hpp and io.hpp). The language bindings,
// which weld and expand the FULL version matrix, declare the range alias
// tables and the instantiation matrix in their own translation units — see
// bindings/python/instantiations/m2_ranges.hpp and m2_matrix.inl.
