#pragma once

/** @file
    The M2 entity (namespace wowlib::formats::m2): a whole model with its
    satellite files baked in, versioned on the client it is laid out for. The
    assembly unifies the MD20 body (M2Root) with the external .skin LOD views
    and re-splits low-priority sequence data back out to .anim files on write
    (driven by the sequence flags). Pre-WotLK everything is one file; Legion+
    adds the chunked wrapper and .skel/.bone/.phys satellites (stage 3). */

#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/m2/bone/bone.hpp>
#include <wowlib/formats/m2/offset_block.hpp>
#include <wowlib/formats/m2/chunked/chunked.hpp>
#include <wowlib/formats/m2/root/root.hpp>
#include <wowlib/formats/m2/satellites.hpp>
#include <wowlib/formats/m2/skeleton.hpp>
#include <wowlib/formats/m2/skin/skin.hpp>
#include <wowlib/fs/filesystem.hpp>

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
                        layout field from this vector's length.)")]]
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

    /** Check the whole model's logical integrity — the body's and every skin's
        own validate() plus the cross-file contracts only the assembly can see:
        each skin's local->global vertex lookup landing inside the body's
        vertex list, and every render batch resolving its material, color,
        texture and transparency through the body's lookup tables. Validation
        is a separate pass — write() never runs it; call this before writing to
        know the model will load in the client. A freshly read, unmodified
        client model reports zero errors. Excluded from the bindings until the
        facade verbs land (stage 4).
        @return every violated contract, prefixed "root"/"skins[i]". */
    [[nodiscard]]
    [[=welder::mark::exclude]]
    ValidationReport validate() const;

    /** The monadic face of validate(), for `ensure_valid().and_then(...)`
        chains before a write.
        @return success when validate() reports no errors; otherwise one
                InvalidEntityState error listing the findings. */
    [[nodiscard]]
    [[=welder::mark::exclude]]
    Result<void> ensure_valid() const;

    bool operator==(const M2&) const = default;

  private:
    // --- internal fs-I/O helpers (definitions at the bottom of this header;
    // --- private so the Python/Lua surface and the C++ API stay verbs-only) -

    /** Verify the MD20 magic and that the file's format version belongs to
        @a V's era.
        @param magic          the leading magic read from the file.
        @param format_version the version field read from the file.
        @return nothing, or FormatVersionMismatch. */
    static Result<void> _check_header(std::uint32_t magic, std::uint32_t format_version);

    /** Load and parse one .skin by key into @a out (a std::vector<Skin<V>>;
        deduced so the pre-WotLK class instantiations — which the bindings
        instantiate EXPLICITLY, member declarations included — never spell
        the era-constrained Skin alias in a signature).
        @param fs   the filesystem gateway.
        @param key  the .skin identity.
        @param what the diagnostic context ("skin 2", "lod skin 1").
        @param out  the vector the parsed skin is appended to.
        @return nothing, or the contextualized error. */
    static Result<void> _read_skin_into(fs::FileSystem& fs, const FileKey& key,
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
    Result<void> _read_monolithic(fs::FileSystem& fs, const FileKey& key,
                                 std::span<const std::byte> main)
      requires (V >= m2_per_sequence_timelines);

    /** The Legion+ chunked read path — an orchestrator over the phase helpers
        below: read the chunk shell, pull the skeleton in when the model is
        skel-based, decode the MD21 body (then drop its transport blob), and
        load the .skin / .bone / .phys satellites.
        @param fs   the filesystem gateway.
        @param key  the model identity.
        @param main the model file bytes.
        @return nothing, or the first error. */
    Result<void> _read_chunked(fs::FileSystem& fs, const FileKey& key,
                              std::span<const std::byte> main)
      requires (V >= m2_chunked_container);

    /** Load the referenced .skel into `skel` (skel-based models only — call
        under _skeleton_engaged); its sequences and AFID/BFID tables then stand
        in for the body's.
        @param fs the filesystem gateway.
        @return nothing, or the contextualized .skel error. */
    Result<void> _load_skeleton(fs::FileSystem& fs)
      requires (V >= m2_chunked_container);

    /** Decode the MD20 body out of the MD21 image with FileDataID-based .anim
        resolution (falling back to name-based when the key resolves to a
        path), then verify its header. Skel-based models resolve their
        sequences and AFIDs from `skel` rather than the body/shell.
        @param fs       the filesystem gateway.
        @param key      the model identity (for the .anim name fallback).
        @param has_skel whether the model is skeleton-based.
        @return nothing, or the first error. */
    Result<void> _read_chunked_body(fs::FileSystem& fs, const FileKey& key, bool has_skel)
      requires (V >= m2_chunked_container);

    /** Load the LOD views from SFID: the first num_skin_profiles entries are
        the .skin views, the remainder the LOD bands (a zero/truncated tail is
        tolerated). Views append to `skins`, bands to `lod_skins`.
        @param fs the filesystem gateway.
        @return nothing, or the first error. */
    Result<void> _read_chunked_skins(fs::FileSystem& fs)
      requires (V >= m2_chunked_container);

    /** Load the .bone files (the skeleton's when skel-based) into `bone_files`.
        @param fs       the filesystem gateway.
        @param has_skel whether the .bone FileDataIDs come from the skeleton.
        @return nothing, or the first contextualized error. */
    Result<void> _read_bone_files(fs::FileSystem& fs, bool has_skel)
      requires (V >= m2_chunked_container);

    /** Bake the referenced .phys file into `phys` verbatim (inline PFDC stays
        a chunk on the stream); physics is optional, so a missing file degrades
        to empty rather than failing.
        @param fs the filesystem gateway. */
    void _read_physics(fs::FileSystem& fs)
      requires (V >= m2_chunked_container);

    /** Serialize the MD20 body into a fresh image, routing each external
        sequence's per-sequence blocks into @a afm2_bufs sinks keyed by
        @a sequences, and stamp the derived num_skin_profiles (skins.size()).
        Shared by the monolithic and chunked write paths.
        @param afm2_bufs [out] per-sequence .anim buffers, filled by the write.
        @param sequences the sequence table driving the split (the body's own,
                         or the skeleton's for skel-based models).
        @return the body image, or the first error. */
    Result<FileBuffer> _write_body_image(AnimBuffers& afm2_bufs, const auto& sequences) const
      requires (V >= m2_per_sequence_timelines);

    /** The monolithic-with-satellites write path (WotLK through WoD): the body
        plus raw .anim payloads and numbered .skin views, all by conventional
        name.
        @param fs    the filesystem gateway.
        @param path  the resolved model path.
        @param paths the satellite naming conventions around @a path.
        @return nothing, or the first error. */
    Result<void> _write_monolithic(fs::FileSystem& fs, const std::string& path,
                                  const SatellitePaths& paths) const
      requires (V >= m2_per_sequence_timelines && V < m2_chunked_container);

    /** The Legion+ chunked write path: re-encode the body, write every
        satellite (so fresh FileDataIDs land in the reference chunks), and
        rebuild the chunk stream around them.
        @param fs    the filesystem gateway.
        @param path  the resolved model path.
        @param paths the satellite naming conventions around @a path.
        @return nothing, or the first error. */
    Result<void> _write_chunked(fs::FileSystem& fs, const std::string& path,
                               const SatellitePaths& paths) const
      requires (V >= m2_chunked_container);

    /** Write the .bone files by conventional name.
        @param fs    the filesystem gateway.
        @param paths the satellite naming conventions.
        @return the allocated .bone FileDataIDs (parallel to bone_files), or
                the first error. */
    Result<std::vector<std::uint32_t>> _write_bone_files(fs::FileSystem& fs,
                                                        const SatellitePaths& paths) const
      requires (V >= m2_chunked_container);

    /** Non-skel .anim write: one file per external sequence (AFM2-wrapped when
        the model requests chunked .anim, else the raw blob), recording the
        fresh FileDataIDs into @a stream.anim_fdids.
        @param fs        the filesystem gateway.
        @param paths     the satellite naming conventions.
        @param afm2_bufs the per-sequence body buffers filled by _write_body_image.
        @param stream    [out] the chunk stream whose AFID refs are rebuilt.
        @return nothing, or the first error. */
    Result<void> _write_plain_anims(fs::FileSystem& fs, const SatellitePaths& paths,
                                   AnimBuffers& afm2_bufs, auto& stream) const
      requires (V >= m2_chunked_container);

    /** Skel-based satellite write: re-encode the skeleton's bone/attachment
        blocks (splitting AFSB/AFSA per sequence), assemble the shared .anim
        files as AFM2 + AFSA + AFSB, write the .skel, and hang every satellite
        FileDataID off the skeleton — so @a stream carries only the SKID
        reference.
        @param fs         the filesystem gateway.
        @param paths      the satellite naming conventions.
        @param afm2_bufs  the body's per-sequence buffers (the AFM2 layer).
        @param sequences  the skeleton's sequence table (drives the AFSA/AFSB splits).
        @param bone_fdids the .bone FileDataIDs to record on the skeleton.
        @param stream     [out] the chunk stream whose SKID is set.
        @return nothing, or the first error. */
    Result<void> _write_skeleton_satellites(fs::FileSystem& fs, const SatellitePaths& paths,
                                           AnimBuffers& afm2_bufs, const auto& sequences,
                                           const std::vector<std::uint32_t>& bone_fdids,
                                           auto& stream) const
      requires (V >= m2_chunked_container);

    /** Write the .skin views and LOD bands, recording their FileDataIDs into
        @a stream.skin_fdids (views first, then bands).
        @param fs     the filesystem gateway.
        @param paths  the satellite naming conventions.
        @param stream [out] the chunk stream whose SFID refs are rebuilt.
        @return nothing, or the first error. */
    Result<void> _write_chunked_skins(fs::FileSystem& fs, const SatellitePaths& paths,
                                      auto& stream) const
      requires (V >= m2_chunked_container);

    /** Bake the .phys file (when present) and record its FileDataID into
        @a stream.phys_fdid.
        @param fs     the filesystem gateway.
        @param paths  the satellite naming conventions.
        @param stream [out] the chunk stream whose PFID ref is rebuilt.
        @return nothing, or the first error. */
    Result<void> _write_chunked_phys(fs::FileSystem& fs, const SatellitePaths& paths,
                                    auto& stream) const
      requires (V >= m2_chunked_container);

    /** Whether the SKID reference chunk actually engages a skeleton — files
        may carry the chunk with a stored 0 meaning "none", and read and
        write MUST agree on this predicate (a mismatch would convert such a
        model into a broken skel-based one on round-trip).
        @param skeleton_fdid the SKID entries.
        @return whether a skeleton is referenced. */
    static bool _skeleton_engaged(std::span<const std::uint32_t> skeleton_fdid)
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

// --- fs-level read/write definitions -----------------------------------------
// Inline in this header (not a separate io.hpp): the entities are templates,
// so the definitions must be visible for implicit instantiation anyway — the
// library ships NO explicit instantiations; every consumer TU instantiates
// exactly the versions it uses (the bindings expand the full matrix in their
// own translation units, see bindings/python/instantiations/).
namespace wowlib::formats::m2
{
  template <ClientVersion V>
  Result<void> detail::M2<V>::_check_header(std::uint32_t magic, std::uint32_t format_version)
  {
    if (magic != md20_magic)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("not an MD20 model (magic {:#010x}; a Legion+ chunked "
                                    "file starts with MD21 instead)",
                                    magic));
    constexpr auto range = m2_format_version_range(V);
    if (format_version < range.first || format_version > range.second)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("MD20 version {} is outside the requested client's "
                                    "era [{}, {}]",
                                    format_version, range.first, range.second));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_read_skin_into(fs::FileSystem& fs, const FileKey& key,
                                             std::string_view what, auto& out)
    requires (V >= m2_per_sequence_timelines)
  {
    const auto bytes = fs.read_file(key);
    if (!bytes)
      return make_error(bytes.error().code, std::format("{}: {}", what, bytes.error().message),
                        bytes.error().native_error);
    Skin<V> skin;
    if (auto r = skin.read(std::span<const std::byte>{*bytes}); !r)
      return make_error(r.error().code, std::format("{}: {}", what, r.error().message),
                        r.error().native_error);
    if (skin.magic != skin_magic)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("{} magic is {:#010x}, expected 'SKIN'", what, skin.magic));
    out.push_back(std::move(skin));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_read_monolithic(fs::FileSystem& fs, const FileKey& key,
                                      std::span<const std::byte> main)
    requires (V >= m2_per_sequence_timelines)
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "a monolithic M2's satellite files (.skin/.anim) need a "
                        "resolvable path for the model key");
    const SatellitePaths paths{*resolved.path};

    // Low-priority sequence data lives in per-sequence .anim files; the
    // context resolves them lazily — the sequences table is populated
    // before any track member reads (layout order), so the flags are already
    // decoded when the first track consults us. A missing .anim file
    // leaves its sequences' tracks empty rather than failing.
    AnimCache cache{[&](SequenceKey seq) {
      return fs.read_file(FileKey{paths.anim(seq)});
    }};
    OffsetReadContext ctx;
    ctx.sequence_base = cache.sequence_base(this->root.sequences, main, AnimCache::afm2_magic);
    if (auto r = this->root.read(main, ctx); !r)
      return r;
    if (auto r = _check_header(this->root.magic, this->root.format_version); !r)
      return r;

    this->skins.reserve(this->root.num_skin_profiles);
    for (std::uint32_t i = 0; i < this->root.num_skin_profiles; ++i)
      if (auto r = _read_skin_into(fs, FileKey{paths.skin(i)},
                                  std::format("skin {}", i), this->skins);
          !r)
        return r;
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_read_chunked(fs::FileSystem& fs, const FileKey& key,
                                   std::span<const std::byte> main)
    requires (V >= m2_chunked_container)
  {
    // 1. the chunk shell (MD21 transport blob + the reference/data chunks).
    if (auto r = this->chunks.read(main); !r)
      return r;

    // 2. the skeleton, when the model is skel-based — its sequences and
    //    AFID/BFID tables then stand in for the body's (which are empty).
    const bool has_skel = _skeleton_engaged(this->chunks.skeleton_fdid);
    if (has_skel)
      if (auto r = _load_skeleton(fs); !r)
        return r;

    // 3. the MD20 body out of the MD21 image; the transport blob is spent once
    //    decoded (the body's md21 span dies inside _read_chunked_body), so drop
    //    it now rather than keep a second whole-model image in memory —
    //    write() re-encodes root into a fresh stream.
    if (auto r = _read_chunked_body(fs, key, has_skel); !r)
      return r;
    this->chunks.md21.bytes = {};

    // 4. the LOD views, then 5. the .bone files and 6. the (optional) physics
    //    blob — a monadic chain, so each phase runs only if the prior succeeded.
    return _read_chunked_skins(fs)
      .and_then([&] { return _read_bone_files(fs, has_skel); })
      .transform([&] { _read_physics(fs); });
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_load_skeleton(fs::FileSystem& fs)
    requires (V >= m2_chunked_container)
  {
    if (auto r = this->skel.read(fs, FileKey{FileDataID{this->chunks.skeleton_fdid.front()}}); !r)
      return make_error(r.error().code, std::format(".skel: {}", r.error().message),
                        r.error().native_error);
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_read_chunked_body(fs::FileSystem& fs, const FileKey& key,
                                                bool has_skel)
    requires (V >= m2_chunked_container)
  {
    // name fallback for satellites without FileDataID entries
    std::optional<SatellitePaths> paths;
    if (const FileKey resolved = fs.resolve(key); resolved.path)
      paths.emplace(*resolved.path);

    const std::span<const std::byte> md21{this->chunks.md21.bytes};

    // skel-based models keep their sequences (and thus the external-data
    // flags) in the skeleton; the body's own table is empty then
    const auto* sequences =
      has_skel ? &this->skel.sequence_block.sequences : &this->root.sequences;
    const auto& afids = has_skel ? this->skel.effective_anim_fdids() : this->chunks.anim_fdids;

    AnimCache cache{[&, paths](SequenceKey seq) -> Result<FileBuffer> {
      const std::uint32_t fdid = AnimCache::afid_lookup(afids, seq);
      if (fdid != 0)
        return fs.read_file(FileKey{FileDataID{fdid}});
      if (paths)
        return fs.read_file(FileKey{paths->anim(seq)});
      return make_error(ErrorCode::FileNotFound, "no AFID entry and no path");
    }};
    OffsetReadContext ctx;
    ctx.sequence_base = cache.sequence_base(*sequences, md21, AnimCache::afm2_magic);
    if (auto r = this->root.read(md21, ctx); !r)
      return r;
    return _check_header(this->root.magic, this->root.format_version);
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_read_chunked_skins(fs::FileSystem& fs)
    requires (V >= m2_chunked_container)
  {
    // the first num_skin_profiles SFID entries are the views, the rest the LOD
    // bands (real files occasionally truncate the LOD tail)
    if (this->chunks.skin_fdids.size() < this->root.num_skin_profiles)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("SFID holds {} entries, the body declares {} views",
                                    this->chunks.skin_fdids.size(),
                                    this->root.num_skin_profiles));
    for (const auto [i, fdid] : std::views::enumerate(this->chunks.skin_fdids)
                                  | std::views::take(this->root.num_skin_profiles))
      if (auto r =
            _read_skin_into(fs, FileKey{FileDataID{fdid}}, std::format("skin {}", i), this->skins);
          !r)
        return r;
    for (const auto [i, fdid] : std::views::enumerate(this->chunks.skin_fdids)
                                  | std::views::drop(this->root.num_skin_profiles))
      if (fdid != 0)
        if (auto r = _read_skin_into(fs, FileKey{FileDataID{fdid}}, std::format("lod skin {}", i),
                                    this->lod_skins);
            !r)
          return r;
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_read_bone_files(fs::FileSystem& fs, bool has_skel)
    requires (V >= m2_chunked_container)
  {
    const auto& bfids = has_skel ? this->skel.effective_bone_fdids() : this->chunks.bone_fdids;
    for (const auto [i, fdid] : std::views::enumerate(bfids))
    {
      if (fdid == 0)
        continue;
      const auto bytes = fs.read_file(FileKey{FileDataID{fdid}});
      if (!bytes)
        return make_error(bytes.error().code,
                          std::format(".bone {}: {}", i, bytes.error().message),
                          bytes.error().native_error);
      BoneFile bone;
      if (auto r = bone.read(std::span<const std::byte>{*bytes}); !r)
        return make_error(r.error().code, std::format(".bone {}: {}", i, r.error().message),
                          r.error().native_error);
      this->bone_files.push_back(std::move(bone));
    }
    return {};
  }

  template <ClientVersion V>
  void detail::M2<V>::_read_physics(fs::FileSystem& fs)
    requires (V >= m2_chunked_container)
  {
    if (!this->chunks.phys_fdid.empty() && this->chunks.phys_fdid.front() != 0)
      if (auto bytes = fs.read_file(FileKey{FileDataID{this->chunks.phys_fdid.front()}}))
        this->phys.bytes = std::move(*bytes);
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto main = fs.read_file(key);
    if (!main)
      return std::unexpected{main.error()};

    root = {};
    if constexpr (V >= m2_per_sequence_timelines)
      this->skins.clear();
    if constexpr (V >= m2_chunked_container)
    {
      this->chunks = {};
      this->lod_skins.clear();
      this->phys = {};
      this->skel = {};
      this->bone_files.clear();
    }

    if constexpr (V < m2_per_sequence_timelines)
    {
      if (auto r = root.read(std::span<const std::byte>{*main}); !r)
        return r;
      return _check_header(root.magic, root.format_version);
    }
    else if constexpr (V < m2_chunked_container)
    {
      return _read_monolithic(fs, key, std::span<const std::byte>{*main});
    }
    else
    {
      std::uint32_t lead = 0;
      if (main->size() >= 4)
        std::memcpy(&lead, main->data(), 4);
      if (lead == md20_magic)
      {
        // Legion clients still served leftover raw MD20 models; from BfA on
        // none exist, so a bare image under a BfA+ target is a mismatch, not
        // a fallback (m2_chunked_only).
        if constexpr (V < m2_chunked_only)
          return _read_monolithic(fs, key, std::span<const std::byte>{*main});
        else
          return make_error(ErrorCode::FormatVersionMismatch,
                            "bare MD20 model under a BfA+ target — raw (unchunked) .m2 files "
                            "no longer exist from 8.0 on; if this is a Legion-era file, read "
                            "it with the legion target");
      }
      return _read_chunked(fs, key, std::span<const std::byte>{*main});
    }
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable, "saving an M2 needs a path for the key");
    const SatellitePaths paths{*resolved.path};

    if constexpr (V < m2_per_sequence_timelines)
    {
      const auto bytes = root.write();
      if (!bytes)
        return std::unexpected{bytes.error()};
      if (auto r = fs.add_file(*resolved.path, *bytes); !r)
        return std::unexpected{r.error()};
      return {};
    }
    else if constexpr (V < m2_chunked_container)
      return _write_monolithic(fs, *resolved.path, paths);
    else
      return _write_chunked(fs, *resolved.path, paths);
  }

  template <ClientVersion V>
  Result<FileBuffer> detail::M2<V>::_write_body_image(AnimBuffers& afm2_bufs,
                                                     const auto& sequences) const
    requires (V >= m2_per_sequence_timelines)
  {
    // Low-priority sequences split back out: every external sequence gets an
    // .anim buffer, filled as the tracks route their per-sequence blocks
    // through the sinks (empty ones still write — the client requests the file
    // whenever the flags say so).
    auto bytes = root.write(afm2_bufs.sink(sequences));
    if (!bytes)
      return std::unexpected{bytes.error()};

    // stamp the derived skin count into the freshly written image — the baked
    // skins vector is the source of truth (num_skin_profiles is a hidden
    // layout field, see M2Root)
    constexpr std::size_t count_at = M2Root<V>::member_offset("num_skin_profiles");
    const auto count = static_cast<std::uint32_t>(this->skins.size());
    std::memcpy(bytes->data() + count_at, &count, sizeof count);
    return bytes;
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_write_monolithic(fs::FileSystem& fs, const std::string& path,
                                               const SatellitePaths& paths) const
    requires (V >= m2_per_sequence_timelines && V < m2_chunked_container)
  {
    AnimBuffers afm2_bufs;
    const auto bytes = _write_body_image(afm2_bufs, root.sequences);
    if (!bytes)
      return std::unexpected{bytes.error()};

    // pre-Legion: the body and raw .anim payloads under conventional names
    if (auto r = fs.add_file(path, *bytes); !r)
      return std::unexpected{r.error()};
    for (const auto& [seq, buf] : afm2_bufs.entries())
      if (auto r = fs.add_file(paths.anim(seq), buf); !r)
        return make_error(r.error().code,
                          std::format("anim {:04}-{:02}: {}", seq.id, seq.variation,
                                      r.error().message),
                          r.error().native_error);
    for (const auto [i, skin] : std::views::enumerate(this->skins))
    {
      const auto skin_bytes = skin.write();
      if (!skin_bytes)
        return std::unexpected{skin_bytes.error()};
      if (auto r = fs.add_file(paths.skin(static_cast<std::uint32_t>(i)), *skin_bytes); !r)
        return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                          r.error().native_error);
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_write_chunked(fs::FileSystem& fs, const std::string& path,
                                            const SatellitePaths& paths) const
    requires (V >= m2_chunked_container)
  {
    // the SAME engagement predicate the read path uses — a stored SKID of 0
    // must not flip a non-skel model into a skel-based write; a skel-based
    // model's sequence table (which drives the .anim split) lives in the skel
    const bool has_skel = _skeleton_engaged(this->chunks.skeleton_fdid);

    AnimBuffers afm2_bufs;
    auto bytes = has_skel ? _write_body_image(afm2_bufs, this->skel.sequence_block.sequences)
                          : _write_body_image(afm2_bufs, root.sequences);
    if (!bytes)
      return std::unexpected{bytes.error()};

    // rebuild the chunk stream around the re-encoded image: satellites write
    // first so their fresh FileDataIDs land in the reference chunks
    M2ChunkedFile<V> stream = this->chunks;
    stream.md21.bytes = std::move(*bytes);

    // .bone files (their fdids land in the skeleton for skel-based models)
    const auto bone_fdids = _write_bone_files(fs, paths);
    if (!bone_fdids)
      return std::unexpected{bone_fdids.error()};

    // .anim files + (for skel models) the .skel; each records its fdids on the
    // stream (non-skel) or the skeleton (skel), rebuilt fresh
    stream.anim_fdids.clear();
    if (!has_skel)
    {
      if (auto r = _write_plain_anims(fs, paths, afm2_bufs, stream); !r)
        return r;
      stream.bone_fdids = *bone_fdids;
    }
    else if (auto r = _write_skeleton_satellites(fs, paths, afm2_bufs,
                                                this->skel.sequence_block.sequences, *bone_fdids,
                                                stream);
             !r)
      return r;

    // the .skin views, then the physics blob, then finalize: serialize the
    // rebuilt stream and store it — a monadic chain that short-circuits on the
    // first failure.
    return _write_chunked_skins(fs, paths, stream)
      .and_then([&] { return _write_chunked_phys(fs, paths, stream); })
      .and_then([&] { return stream.write(); })
      .and_then([&](const FileBuffer& stream_bytes) {
        return fs.add_file(path, stream_bytes).transform([](FileDataID) {});
      });
  }

  template <ClientVersion V>
  Result<std::vector<std::uint32_t>>
  detail::M2<V>::_write_bone_files(fs::FileSystem& fs, const SatellitePaths& paths) const
    requires (V >= m2_chunked_container)
  {
    std::vector<std::uint32_t> bone_fdids;
    for (const auto [i, bone] : std::views::enumerate(this->bone_files))
    {
      const auto bone_bytes = bone.write();
      if (!bone_bytes)
        return std::unexpected{bone_bytes.error()};
      const auto r = fs.add_file(paths.bone(static_cast<std::uint32_t>(i)), *bone_bytes);
      if (!r)
        return make_error(r.error().code, std::format(".bone {}: {}", i, r.error().message),
                          r.error().native_error);
      bone_fdids.push_back(r->value);
    }
    return bone_fdids;
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_write_plain_anims(fs::FileSystem& fs, const SatellitePaths& paths,
                                                AnimBuffers& afm2_bufs,
                                                auto& stream) const
    requires (V >= m2_chunked_container)
  {
    // .anim payloads, AFM2-wrapped when the model asks for chunked ones
    for (const auto& [seq, buf] : afm2_bufs.entries())
    {
      FileBuffer file;
      if (has_flag(root.global_flags, GlobalFlags::ChunkedAnimFiles))
        afm2_bufs.append_chunk_to(file, AnimCache::afm2_magic, seq);
      else
        file = buf;
      const auto r = fs.add_file(paths.anim(seq), file);
      if (!r)
        return make_error(r.error().code,
                          std::format("anim {:04}-{:02}: {}", seq.id, seq.variation,
                                      r.error().message),
                          r.error().native_error);
      stream.anim_fdids.push_back({seq.id, seq.variation, r->value});
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_write_skeleton_satellites(
    fs::FileSystem& fs, const SatellitePaths& paths, AnimBuffers& afm2_bufs,
    const auto& sequences, const std::vector<std::uint32_t>& bone_fdids,
    auto& stream) const
    requires (V >= m2_chunked_container)
  {
    // re-encode the skeleton's bone/attachment blocks (splitting AFSB/AFSA per
    // sequence), then assemble the shared .anim files as AFM2 (body events) +
    // AFSA (attachments) + AFSB (bones) and hang every satellite id off the
    // skeleton.
    // deduced: inside m2::detail the bare Skeleton names the RAW template, but
    // the member is the collapsed m2::Skeleton alias type
    auto skel_copy = this->skel;
    AnimBuffers afsa_bufs;
    AnimBuffers afsb_bufs;
    {
      auto encoded = this->skel.bone_block.write(afsb_bufs.sink(sequences));
      if (!encoded)
        return std::unexpected{encoded.error()};
      skel_copy.skb1.bytes = std::move(*encoded);
      auto attachments = this->skel.attachment_block.write(afsa_bufs.sink(sequences));
      if (!attachments)
        return std::unexpected{attachments.error()};
      skel_copy.ska1.bytes = std::move(*attachments);
    }

    skel_copy.anim_fdids.clear();
    for (const SequenceKey seq : AnimBuffers::merged_keys({&afm2_bufs, &afsa_bufs, &afsb_bufs}))
    {
      FileBuffer file;
      afm2_bufs.append_chunk_to(file, AnimCache::afm2_magic, seq);
      afsa_bufs.append_chunk_to(file, AnimCache::afsa_magic, seq);
      afsb_bufs.append_chunk_to(file, AnimCache::afsb_magic, seq);
      const auto r = fs.add_file(paths.anim(seq), file);
      if (!r)
        return make_error(r.error().code,
                          std::format("anim {:04}-{:02}: {}", seq.id, seq.variation,
                                      r.error().message),
                          r.error().native_error);
      skel_copy.anim_fdids.push_back({seq.id, seq.variation, r->value});
    }
    skel_copy.bone_fdids = bone_fdids;

    const auto skel_bytes = skel_copy.template ChunkedFile<m2::Skeleton<V>>::write();
    if (!skel_bytes)
      return std::unexpected{skel_bytes.error()};
    const auto r = fs.add_file(paths.skel(), *skel_bytes);
    if (!r)
      return make_error(r.error().code, std::format(".skel: {}", r.error().message),
                        r.error().native_error);
    stream.skeleton_fdid.assign(1, r->value);
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_write_chunked_skins(fs::FileSystem& fs, const SatellitePaths& paths,
                                                  auto& stream) const
    requires (V >= m2_chunked_container)
  {
    stream.skin_fdids.clear();
    for (const auto [i, skin] : std::views::enumerate(this->skins))
    {
      const auto skin_bytes = skin.write();
      if (!skin_bytes)
        return std::unexpected{skin_bytes.error()};
      const auto r = fs.add_file(paths.skin(static_cast<std::uint32_t>(i)), *skin_bytes);
      if (!r)
        return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                          r.error().native_error);
      stream.skin_fdids.push_back(r->value);
    }
    for (const auto [i, skin] : std::views::enumerate(this->lod_skins))
    {
      const auto skin_bytes = skin.write();
      if (!skin_bytes)
        return std::unexpected{skin_bytes.error()};
      const auto r = fs.add_file(paths.lod_skin(static_cast<std::uint32_t>(i) + 1), *skin_bytes);
      if (!r)
        return make_error(r.error().code, std::format("lod skin {}: {}", i, r.error().message),
                          r.error().native_error);
      stream.skin_fdids.push_back(r->value);
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_write_chunked_phys(fs::FileSystem& fs, const SatellitePaths& paths,
                                                 auto& stream) const
    requires (V >= m2_chunked_container)
  {
    stream.phys_fdid.clear();
    if (!this->phys.bytes.empty())
    {
      const auto r = fs.add_file(paths.phys(), this->phys.bytes);
      if (!r)
        return make_error(r.error().code, std::format(".phys: {}", r.error().message),
                          r.error().native_error);
      stream.phys_fdid.push_back(r->value);
    }
    return {};
  }

  namespace detail
  {
    /** Check one skin profile's references INTO the model body — the half of a
        skin's contracts that needs both files: the local->global vertex lookup
        and the lookup-table slices every render batch and submesh addresses.
        A profile's self-contained contracts (submesh ranges, batch submesh
        indices) are its own validate_extra.
        @param profile the LOD view's tables.
        @param root    the model body the profile refers into.
        @param report  the report findings land in. */
    template <typename Profile, typename Root>
    void validate_profile_against_root(const Profile& profile, const Root& root,
                                       ValidationReport& report)
    {
      formats::detail::validate_index_elements(profile.vertices, root.vertices.size(), "vertices",
                                               "the model vertices", report);

      for (std::size_t i = 0; i < profile.submeshes.size() && !report.full(); ++i)
      {
        const auto& submesh = profile.submeshes[i];
        if (std::size_t{submesh.bone_combo_index} + submesh.bone_count
            > root.bone_lookup_table.size())
          report.add_error(std::format("submeshes[{}]", i),
                           std::format("bone-lookup range [{}, {}) overruns the {} entries",
                                       submesh.bone_combo_index,
                                       submesh.bone_combo_index + submesh.bone_count,
                                       root.bone_lookup_table.size()));
      }

      for (std::size_t i = 0; i < profile.batches.size() && !report.full(); ++i)
      {
        const auto& batch = profile.batches[i];
        const std::string path = std::format("batches[{}]", i);
        if (batch.material_index >= root.materials.size())
          report.add_error(path, std::format("material_index {} out of range: {} materials",
                                             batch.material_index, root.materials.size()));
        if (!formats::detail::is_no_index(batch.color_index)
            && batch.color_index >= root.colors.size())
          report.add_error(path, std::format("color_index {} out of range: {} colors",
                                             batch.color_index, root.colors.size()));

        // A batch addresses `texture_count` consecutive TEXTURE-lookup entries.
        // The weight and transform lookups do NOT follow texture_count — real
        // files routinely declare a longer run than those tables hold (a 3.3.5a
        // humanmale batch spans 6 over a 2-entry transparency table, the
        // Northrend glue screen 2 over 10 from index 9), so only their starting
        // entry is validated, and only when the table exists at all.
        if (std::size_t{batch.texture_combo_index} + batch.texture_count
            > root.texture_lookup_table.size())
          report.add_error(path,
                           std::format("texture-lookup range [{}, {}) overruns the {} entries",
                                       batch.texture_combo_index,
                                       batch.texture_combo_index + batch.texture_count,
                                       root.texture_lookup_table.size()));
        const auto lookup_start = [&](std::uint16_t first, const auto& table,
                                      std::string_view what) {
          if (!table.empty() && first >= table.size())
            report.add_error(path, std::format("{} start {} out of range: {} entries", what,
                                               first, table.size()));
        };
        lookup_start(batch.texture_weight_combo_index, root.transparency_lookup_table,
                     "transparency-lookup");
        lookup_start(batch.texture_transform_combo_index, root.texture_transforms_lookup_table,
                     "texture-transform-lookup");
      }
    }
  }

  template <ClientVersion V>
  ValidationReport detail::M2<V>::validate() const
  {
    ValidationReport report;
    {
      const std::size_t mark = report.size();
      formats::detail::validate_entity(root, report);
      report.prefix_from(mark, "root");
    }

    // The bone lookups address whichever list actually supplies the bones: a
    // skel-based model (Legion+) leaves root.bones empty and keeps them in the
    // .skel, so only the assembly can resolve this.
    {
      const auto* bones = &root.bones;
      if constexpr (requires { this->skel.bone_block.bones; })
        if (root.bones.empty() && !this->skel.bone_block.bones.empty())
          bones = &this->skel.bone_block.bones;
      formats::detail::validate_optional_index_elements(
        root.bone_lookup_table, bones->size(), "root.bone_lookup_table", "the model bones",
        report);
      formats::detail::validate_optional_index_elements(
        root.key_bone_lookup, bones->size(), "root.key_bone_lookup", "the model bones", report);
    }

    // the LOD views: external .skin files WotLK+, embedded profiles before
    if constexpr (requires { this->skins; })
    {
      const auto walk = [&](const auto& views, std::string_view what) {
        for (std::size_t i = 0; i < views.size() && !report.full(); ++i)
        {
          const std::size_t mark = report.size();
          formats::detail::validate_entity(views[i], report);
          detail::validate_profile_against_root(views[i].profile, root, report);
          report.prefix_from(mark, std::format("{}[{}]", what, i));
        }
      };
      walk(this->skins, "skins");
      if constexpr (requires { this->lod_skins; })  // the LOD bands are Legion+
        walk(this->lod_skins, "lod_skins");
    }
    else
    {
      for (std::size_t i = 0; i < root.skin_profiles.size() && !report.full(); ++i)
      {
        const std::size_t mark = report.size();
        detail::validate_profile_against_root(root.skin_profiles[i], root, report);
        report.prefix_from(mark, std::format("root.skin_profiles[{}]", i));
      }
    }
    return report;
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::ensure_valid() const
  {
    return validate().to_result();
  }
}

// There are NO welded per-range alias tables, extern-template declarations or
// explicit instantiations here: C++ consumer TUs implicitly instantiate
// exactly the versions they use (the read/write definitions live in this
// header and the serializer engines). The language bindings,
// which weld and expand the FULL version matrix, declare the range alias
// tables and the instantiation matrix in their own translation units — see
// bindings/python/instantiations/m2_ranges.hpp and m2_matrix.inl.
