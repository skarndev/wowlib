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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/common/offset_serializer.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/m2/bone/bone.hpp>
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
    // --- internal fs-I/O helpers (definitions at the bottom of this header;
    // --- private so the Python/Lua surface and the C++ API stay verbs-only) -

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

// --- fs-level read/write definitions -----------------------------------------
// Inline in this header (not a separate io.hpp): the entities are templates,
// so the definitions must be visible for implicit instantiation anyway — the
// library ships NO explicit instantiations; every consumer TU instantiates
// exactly the versions it uses (the bindings expand the full matrix in their
// own translation units, see bindings/python/instantiations/).
namespace wowlib::formats::m2
{
  template <ClientVersion V>
  Result<void> detail::M2<V>::check_header(std::uint32_t magic, std::uint32_t format_version)
  {
    if (magic != md20_magic)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("not an MD20 model (magic {:#010x}; a Legion+ chunked "
                                    "file starts with MD21 instead)",
                                    magic));
    constexpr auto range = m2_wire_version_range(V);
    if (format_version < range.first || format_version > range.second)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("MD20 version {} is outside the requested client's "
                                    "era [{}, {}]",
                                    format_version, range.first, range.second));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::read_skin_into(fs::FileSystem& fs, const FileKey& key,
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
  Result<void> detail::M2<V>::read_monolithic(fs::FileSystem& fs, const FileKey& key,
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
    // before any track member reads (wire order), so the flags are already
    // decoded when the first track consults us. A missing .anim file
    // leaves its sequences' tracks empty rather than failing.
    AnimCache cache{[&](SequenceKey seq) {
      return fs.read_file(FileKey{paths.anim(seq)});
    }};
    OffsetReadContext ctx;
    ctx.sequence_base = cache.sequence_base(this->root.sequences, main, AnimCache::afm2_magic);
    if (auto r = this->root.read(main, ctx); !r)
      return r;
    if (auto r = check_header(this->root.magic, this->root.format_version); !r)
      return r;

    this->skins.reserve(this->root.num_skin_profiles);
    for (std::uint32_t i = 0; i < this->root.num_skin_profiles; ++i)
      if (auto r = read_skin_into(fs, FileKey{paths.skin(i)},
                                  std::format("skin {}", i), this->skins);
          !r)
        return r;
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::read_chunked(fs::FileSystem& fs, const FileKey& key,
                                   std::span<const std::byte> main)
    requires (V >= m2_chunked_container)
  {
    if (auto r = this->chunks.read(main); !r)
      return r;

    const bool has_skel = skeleton_engaged(this->chunks.skeleton_fdid);
    if (has_skel)
      if (auto r = this->skel.read(fs, FileKey{FileDataID{this->chunks.skeleton_fdid.front()}});
          !r)
        return make_error(r.error().code, std::format(".skel: {}", r.error().message),
                          r.error().native_error);

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
    if (auto r = check_header(this->root.magic, this->root.format_version); !r)
      return r;

    // skins: the first num_skin_profiles SFID entries are the views, the
    // rest the LOD bands (real files occasionally truncate the LOD tail)
    if (this->chunks.skin_fdids.size() < this->root.num_skin_profiles)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("SFID holds {} entries, the body declares {} views",
                                    this->chunks.skin_fdids.size(),
                                    this->root.num_skin_profiles));
    for (std::uint32_t i = 0; i < this->root.num_skin_profiles; ++i)
      if (auto r = read_skin_into(fs, FileKey{FileDataID{this->chunks.skin_fdids[i]}},
                                  std::format("skin {}", i), this->skins);
          !r)
        return r;
    for (std::size_t i = this->root.num_skin_profiles; i < this->chunks.skin_fdids.size(); ++i)
      if (this->chunks.skin_fdids[i] != 0)
        if (auto r = read_skin_into(fs, FileKey{FileDataID{this->chunks.skin_fdids[i]}},
                                    std::format("lod skin {}", i), this->lod_skins);
            !r)
          return r;

    // .bone files (the skeleton's when skel-based)
    const auto& bfids = has_skel ? this->skel.effective_bone_fdids() : this->chunks.bone_fdids;
    for (std::size_t i = 0; i < bfids.size(); ++i)
    {
      if (bfids[i] == 0)
        continue;
      const auto bytes = fs.read_file(FileKey{FileDataID{bfids[i]}});
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

    // physics: referenced file baked in verbatim (inline PFDC stays a chunk
    // on the stream); a missing file degrades to empty
    if (!this->chunks.phys_fdid.empty() && this->chunks.phys_fdid.front() != 0)
      if (auto bytes = fs.read_file(FileKey{FileDataID{this->chunks.phys_fdid.front()}}))
        this->phys.bytes = std::move(*bytes);

    // the MD21 transport blob is spent once the body is decoded — drop it
    // rather than keep a second whole-model image in memory; write()
    // re-encodes root into the stream (the md21 span above dies with it,
    // hence last)
    this->chunks.md21.bytes = {};
    return {};
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
      return check_header(root.magic, root.format_version);
    }
    else if constexpr (V < m2_chunked_container)
    {
      return read_monolithic(fs, key, std::span<const std::byte>{*main});
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
          return read_monolithic(fs, key, std::span<const std::byte>{*main});
        else
          return make_error(ErrorCode::FormatVersionMismatch,
                            "bare MD20 model under a BfA+ target — raw (unchunked) .m2 files "
                            "no longer exist from 8.0 on; if this is a Legion-era file, read "
                            "it with the legion target");
      }
      return read_chunked(fs, key, std::span<const std::byte>{*main});
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
    else
    {
      // whose sequence table drives the .anim split
      const auto* sequences = &root.sequences;
      bool has_skel = false;
      if constexpr (V >= m2_chunked_container)
      {
        // the SAME engagement predicate the read path uses — a stored SKID of
        // 0 must not flip a non-skel model into a skel-based write
        has_skel = skeleton_engaged(this->chunks.skeleton_fdid);
        if (has_skel)
          sequences = &this->skel.sequence_block.sequences;
      }

      // Low-priority sequences split back out: every external sequence gets
      // an .anim buffer, filled as the tracks route their per-sequence
      // blocks through the sinks (empty ones still write — the client
      // requests the file whenever the flags say so).
      AnimBuffers afm2_bufs;
      const auto make_sink = [sequences](AnimBuffers& bufs) {
        return bufs.sink(*sequences);
      };
      auto bytes = root.write(make_sink(afm2_bufs));
      if (!bytes)
        return std::unexpected{bytes.error()};

      // stamp the derived skin count into the freshly written image — the
      // baked skins vector is the source of truth (num_skin_profiles is a
      // hidden wire field, see M2Root)
      {
        constexpr std::size_t count_at = wire_offset_of<M2Root<V>>("num_skin_profiles");
        const auto count = static_cast<std::uint32_t>(this->skins.size());
        std::memcpy(bytes->data() + count_at, &count, sizeof count);
      }

      if constexpr (V < m2_chunked_container)
      {
        // pre-Legion: raw .anim payloads under conventional names
        if (auto r = fs.add_file(*resolved.path, *bytes); !r)
          return std::unexpected{r.error()};
        for (const auto& [seq, buf] : afm2_bufs.entries())
          if (auto r = fs.add_file(paths.anim(seq), buf); !r)
            return make_error(r.error().code,
                              std::format("anim {:04}-{:02}: {}", seq.id, seq.variation,
                                          r.error().message),
                              r.error().native_error);
        for (std::size_t i = 0; i < this->skins.size(); ++i)
        {
          const auto skin_bytes = this->skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          if (auto r = fs.add_file(paths.skin(static_cast<std::uint32_t>(i)), *skin_bytes);
              !r)
            return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                              r.error().native_error);
        }
        return {};
      }
      else
      {
        // rebuild the chunk stream around the re-encoded image: satellites
        // write first so their fresh FileDataIDs land in the reference chunks
        M2ChunkedFile<V> stream = this->chunks;
        stream.md21.bytes = std::move(*bytes);

        // .bone files (fdids land in the skeleton for skel-based models)
        std::vector<std::uint32_t> bone_fdids;
        for (std::size_t i = 0; i < this->bone_files.size(); ++i)
        {
          const auto bone_bytes = this->bone_files[i].write();
          if (!bone_bytes)
            return std::unexpected{bone_bytes.error()};
          const auto r = fs.add_file(paths.bone(static_cast<std::uint32_t>(i)), *bone_bytes);
          if (!r)
            return make_error(r.error().code,
                              std::format(".bone {}: {}", i, r.error().message),
                              r.error().native_error);
          bone_fdids.push_back(r->value);
        }

        stream.anim_fdids.clear();
        if (!has_skel)
        {
          // .anim payloads, AFM2-wrapped when the model asks for chunked ones
          for (const auto& [seq, buf] : afm2_bufs.entries())
          {
            FileBuffer file;
            if ((root.global_flags & 0x2000u) != 0)
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
          stream.bone_fdids = bone_fdids;
        }
        else
        {
          // skel-based: re-encode the skeleton blocks, then assemble the
          // shared .anim files as AFM2 (body events) + AFSA (attachments) +
          // AFSB (bones) and hang every satellite id off the skeleton
          // deduced: inside m2::detail the bare Skeleton names the RAW
          // template, but the member is the collapsed m2::Skeleton alias type
          auto skel_copy = this->skel;
          AnimBuffers afsa_bufs;
          AnimBuffers afsb_bufs;
          {
            auto encoded = this->skel.bone_block.write(make_sink(afsb_bufs));
            if (!encoded)
              return std::unexpected{encoded.error()};
            skel_copy.skb1.bytes = std::move(*encoded);
            auto attachments = this->skel.attachment_block.write(make_sink(afsa_bufs));
            if (!attachments)
              return std::unexpected{attachments.error()};
            skel_copy.ska1.bytes = std::move(*attachments);
          }

          skel_copy.anim_fdids.clear();
          for (const SequenceKey seq :
               AnimBuffers::merged_keys({&afm2_bufs, &afsa_bufs, &afsb_bufs}))
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

          const auto skel_bytes = skel_copy.ChunkedFile<m2::Skeleton<V>>::write();
          if (!skel_bytes)
            return std::unexpected{skel_bytes.error()};
          const auto r = fs.add_file(paths.skel(), *skel_bytes);
          if (!r)
            return make_error(r.error().code, std::format(".skel: {}", r.error().message),
                              r.error().native_error);
          stream.skeleton_fdid.assign(1, r->value);
        }

        stream.skin_fdids.clear();
        for (std::size_t i = 0; i < this->skins.size(); ++i)
        {
          const auto skin_bytes = this->skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          const auto r = fs.add_file(paths.skin(static_cast<std::uint32_t>(i)), *skin_bytes);
          if (!r)
            return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                              r.error().native_error);
          stream.skin_fdids.push_back(r->value);
        }
        for (std::size_t i = 0; i < this->lod_skins.size(); ++i)
        {
          const auto skin_bytes = this->lod_skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          const auto r =
            fs.add_file(paths.lod_skin(static_cast<std::uint32_t>(i) + 1), *skin_bytes);
          if (!r)
            return make_error(r.error().code,
                              std::format("lod skin {}: {}", i, r.error().message),
                              r.error().native_error);
          stream.skin_fdids.push_back(r->value);
        }

        stream.phys_fdid.clear();
        if (!this->phys.bytes.empty())
        {
          const auto r = fs.add_file(paths.phys(), this->phys.bytes);
          if (!r)
            return make_error(r.error().code, std::format(".phys: {}", r.error().message),
                              r.error().native_error);
          stream.phys_fdid.push_back(r->value);
        }

        const auto stream_bytes = stream.write();
        if (!stream_bytes)
          return std::unexpected{stream_bytes.error()};
        if (auto r = fs.add_file(*resolved.path, *stream_bytes); !r)
          return std::unexpected{r.error()};
        return {};
      }
    }
  }
}

// There are NO welded per-range alias tables, extern-template declarations or
// explicit instantiations here: C++ consumer TUs implicitly instantiate
// exactly the versions they use (the read/write definitions live in this
// header and the serializer engines). The language bindings,
// which weld and expand the FULL version matrix, declare the range alias
// tables and the instantiation matrix in their own translation units — see
// bindings/python/instantiations/m2_ranges.hpp and m2_matrix.inl.
