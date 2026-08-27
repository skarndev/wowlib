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
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/file_entity.hpp>
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

namespace wowlib::formats::m2 {
  using root::GlobalFlags;
  using root::Md20Magic;
  using bone::BoneFile;
  using skin::Skin;
  using skin::SkinMagic;

  /** The version-agnostic base of every M2<V> (welded as "M2").

      This empty base exists ENTIRELY for the language bindings (Python, Lua):
      it gives the per-version M2* classes a common welded supertype, and the
      module glue attaches the for_version/read/write/convert surface to it.
      It has no role in the C++ API, where you use the concrete M2<V>
      directly.

      @see https://wowdev.wiki/M2 */
  struct [[
      =welder::weld,
      =welder::weld_as("M2"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A whole model, abstract over the client version — the MD20 body with
        its satellite files (.skin, .anim) baked in. Construct the concrete
        version with M2.for_version(expansion), then read()/write(); the
        per-version M2* classes are subclasses. See https://wowdev.wiki/M2.)")
    ]] M2Base : FileEntityBase {
    bool operator==(const M2Base&) const = default;
  };

  namespace detail {
    // The trait primaries are EMPTY and unconstrained: the binding walk
    // completes absent<Trait>'s template argument even for the versions a
    // slot leaves inactive, so an out-of-era instantiation must be valid —
    // the members live in constrained partial specializations instead.

    /** WotLK+ assembly members: the external .skin LOD views baked in. */
    template <ClientVersion V>
    struct AssemblySkins {
      [[=welder::mark::exclude]]
      bool operator==(const AssemblySkins&) const = default;
    };

    template <ClientVersion V> requires (V >= M2PerSequenceTimelines)
    struct AssemblySkins<V> {
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

    /** legion+ assembly members: the chunk stream and the satellites it
        references. */
    template <ClientVersion V>
    struct AssemblyLegion {
      [[=welder::mark::exclude]]
      bool operator==(const AssemblyLegion&) const = default;
    };

    template <ClientVersion V> requires (V >= M2ChunkedContainer)
    struct AssemblyLegion<V> {
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
      std::vector<Skin<V>> lodSkins;

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
      std::vector<BoneFile> boneFiles;

      [[=welder::mark::exclude]]
      bool operator==(const AssemblyLegion&) const = default;
    };
  }

  namespace detail {
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
        =welder::weld,
        =welder::doc(R"(
        A whole model for one client version: the MD20 body with skins and
        external sequence data baked in. A written model is canonical-layout
        and re-reads equal (no byte-perfect guarantee for offset formats).
        See https://wowdev.wiki/M2.)")
      ]] M2 : M2Base,
              Slot<V, M2PerSequenceTimelines, detail::AssemblySkins<V>>,
              Slot<V, M2ChunkedContainer, detail::AssemblyLegion<V>> {
      static constexpr ClientVersion Version = V;

      [[=welder::doc("The MD20 body — the model's root, uniform across every "
        "era (pre-Legion it IS the file; Legion+ it is the "
        "decoded MD21 image, whose transport blob `chunks` drops "
        "after read).")]]
      M2Root<V> root{};

      // read()/write() weld the (FileSystem, FileKey) load/save on LUA AND C#
      // ONLY — mirroring WMO: on Python the module glue attaches the richer
      // read/write/convert/for_version surface to M2Base instead (stage 4).
      // Lua and C# have no such glue, so they take these methods directly.

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc("Load the model and all its satellite files from a client "
          "filesystem, replacing this entity's contents.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key [[=welder::doc("the .m2 file identity (path and/or FileDataID)")]]);

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc("Serialize and store the model and every satellite file "
          "through the filesystem's project overlay; satellite names "
          "derive from the key, which must resolve to a path.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key
        [[=welder::doc(
          "the .m2 file identity; must resolve to a path")]]) const;

      // Beyond the body's and each skin's own contracts, this sees the ones only
      // the assembly can: each skin's local->global vertex lookup landing inside
      // the body's vertices, every batch resolving its material, color and
      // lookup slices, and the bone lookups against whichever list supplies the
      // bones (a skel-based model keeps them in the .skel).
      [[nodiscard]]
      [[=welder::doc(R"(
        Check the logical integrity contracts this model must satisfy to LOAD
        in the client — across the body AND every skin — which write()
        deliberately never enforces. Call it before writing when you want to
        know the model will load. A model read from a client and left
        unmodified reports no errors; warnings mark states real client files
        ship.)"),
        =welder::returns(R"(every violated contract, each with its member path
                          ("root..." / "skins[i]..."))")]]
      ValidationReport validate() const;

      [[nodiscard]]
      [[=welder::doc(
          "Validate and raise on the first error instead of returning "
          "a report — the assert-style face of validate()."),
        =welder::returns("nothing; raises when validate() finds any error")]]
      Result<void> ensureValid() const;

      bool operator==(const M2&) const = default;

    private:
      // --- internal fs-I/O helpers (definitions at the bottom of this header;
      // --- private so the Python/Lua surface and the C++ API stay verbs-only) -

      /** Verify the MD20 magic and that the file's format version belongs to
          @a V's era.
          @param magic          the leading magic read from the file.
          @param formatVersion the version field read from the file.
          @return nothing, or FormatVersionMismatch. */
      static Result<void> _checkHeader(std::uint32_t magic, std::uint32_t formatVersion);

      /** Load and parse one .skin by key into @a out (a std::vector<Skin<V>>;
          deduced so the pre-WotLK class instantiations — which the bindings
          instantiate EXPLICITLY, member declarations included — never spell
          the era-constrained Skin alias in a signature).
          @param fs   the filesystem gateway.
          @param key  the .skin identity.
          @param what the diagnostic context ("skin 2", "lod skin 1").
          @param out  the vector the parsed skin is appended to.
          @return nothing, or the contextualized error. */
      static Result<void> _readSkinInto(fs::FileSystem& fs, const FileKey& key, std::string_view what, auto& out)
        requires (V >= M2PerSequenceTimelines);

      /** The read path shared by every monolithic-with-satellites era (WotLK
          through pre-Legion, and Legion-era files still shipping raw MD20):
          decode the body with name-based .anim resolution, then load the
          numbered .skin views.
          @param fs   the filesystem gateway.
          @param key  the model identity.
          @param main the model file bytes.
          @return nothing, or the first error. */
      Result<void> _readMonolithic(fs::FileSystem& fs, const FileKey& key, std::span<const std::byte> main) requires (V
        >= M2PerSequenceTimelines);

      /** The Legion+ chunked read path — an orchestrator over the phase helpers
          below: read the chunk shell, pull the skeleton in when the model is
          skel-based, decode the MD21 body (then drop its transport blob), and
          load the .skin / .bone / .phys satellites.
          @param fs   the filesystem gateway.
          @param key  the model identity.
          @param main the model file bytes.
          @return nothing, or the first error. */
      Result<void> _readChunked(fs::FileSystem& fs, const FileKey& key, std::span<const std::byte> main) requires (V >=
        M2ChunkedContainer);

      /** Load the referenced .skel into `skel` (skel-based models only — call
          under _skeletonEngaged); its sequences and AFID/BFID tables then stand
          in for the body's.
          @param fs the filesystem gateway.
          @return nothing, or the contextualized .skel error. */
      Result<void> _loadSkeleton(fs::FileSystem& fs) requires (V >= M2ChunkedContainer);

      /** Decode the MD20 body out of the MD21 image with FileDataID-based .anim
          resolution (falling back to name-based when the key resolves to a
          path), then verify its header. Skel-based models resolve their
          sequences and AFIDs from `skel` rather than the body/shell.
          @param fs       the filesystem gateway.
          @param key      the model identity (for the .anim name fallback).
          @param hasSkel whether the model is skeleton-based.
          @return nothing, or the first error. */
      Result<void> _readChunkedBody(fs::FileSystem& fs, const FileKey& key, bool hasSkel) requires (V >=
        M2ChunkedContainer);

      /** Load the LOD views from SFID: the first numSkinProfiles entries are
          the .skin views, the remainder the LOD bands (a zero/truncated tail is
          tolerated). Views append to `skins`, bands to `lodSkins`.
          @param fs the filesystem gateway.
          @return nothing, or the first error. */
      Result<void> _readChunkedSkins(fs::FileSystem& fs) requires (V >= M2ChunkedContainer);

      /** Load the .bone files (the skeleton's when skel-based) into `boneFiles`.
          @param fs       the filesystem gateway.
          @param hasSkel whether the .bone FileDataIDs come from the skeleton.
          @return nothing, or the first contextualized error. */
      Result<void> _readBoneFiles(fs::FileSystem& fs, bool hasSkel) requires (V >= M2ChunkedContainer);

      /** Bake the referenced .phys file into `phys` verbatim (inline PFDC stays
          a chunk on the stream); physics is optional, so a missing file degrades
          to empty rather than failing.
          @param fs the filesystem gateway. */
      void _readPhysics(fs::FileSystem& fs) requires (V >= M2ChunkedContainer);

      /** Serialize the MD20 body into a fresh image, routing each external
          sequence's per-sequence blocks into @a afm2Bufs sinks keyed by
          @a sequences, and stamp the derived numSkinProfiles (skins.size()).
          Shared by the monolithic and chunked write paths.
          @param afm2Bufs [out] per-sequence .anim buffers, filled by the write.
          @param sequences the sequence table driving the split (the body's own,
                           or the skeleton's for skel-based models).
          @return the body image, or the first error. */
      Result<FileBuffer> _writeBodyImage(AnimBuffers& afm2Bufs, const auto& sequences) const requires (V >=
        M2PerSequenceTimelines);

      /** The monolithic-with-satellites write path (WotLK through WoD): the body
          plus raw .anim payloads and numbered .skin views, all by conventional
          name.
          @param fs    the filesystem gateway.
          @param path  the resolved model path.
          @param paths the satellite naming conventions around @a path.
          @return nothing, or the first error. */
      Result<void> _writeMonolithic(fs::FileSystem& fs, const std::string& path, const SatellitePaths& paths) const
        requires (V >= M2PerSequenceTimelines && V < M2ChunkedContainer);

      /** The Legion+ chunked write path: re-encode the body, write every
          satellite (so fresh FileDataIDs land in the reference chunks), and
          rebuild the chunk stream around them.
          @param fs    the filesystem gateway.
          @param path  the resolved model path.
          @param paths the satellite naming conventions around @a path.
          @return nothing, or the first error. */
      Result<void> _writeChunked(fs::FileSystem& fs, const std::string& path, const SatellitePaths& paths) const
        requires (V >= M2ChunkedContainer);

      /** Write the .bone files by conventional name.
          @param fs    the filesystem gateway.
          @param paths the satellite naming conventions.
          @return the allocated .bone FileDataIDs (parallel to boneFiles), or
                  the first error. */
      Result<std::vector<std::uint32_t>> _writeBoneFiles(fs::FileSystem& fs, const SatellitePaths& paths) const
        requires (V >= M2ChunkedContainer);

      /** Non-skel .anim write: one file per external sequence (AFM2-wrapped when
          the model requests chunked .anim, else the raw blob), recording the
          fresh FileDataIDs into @a stream.animFdids.
          @param fs        the filesystem gateway.
          @param paths     the satellite naming conventions.
          @param afm2Bufs the per-sequence body buffers filled by _writeBodyImage.
          @param stream    [out] the chunk stream whose AFID refs are rebuilt.
          @return nothing, or the first error. */
      Result<void> _writePlainAnims(fs::FileSystem& fs,
                                      const SatellitePaths& paths,
                                      AnimBuffers& afm2Bufs,
                                      auto& stream) const requires (V >= M2ChunkedContainer);

      /** Skel-based satellite write: re-encode the skeleton's bone/attachment
          blocks (splitting AFSB/AFSA per sequence), assemble the shared .anim
          files as AFM2 + AFSA + AFSB, write the .skel, and hang every satellite
          FileDataID off the skeleton — so @a stream carries only the SKID
          reference.
          @param fs         the filesystem gateway.
          @param paths      the satellite naming conventions.
          @param afm2Bufs  the body's per-sequence buffers (the AFM2 layer).
          @param sequences  the skeleton's sequence table (drives the AFSA/AFSB splits).
          @param boneFdids the .bone FileDataIDs to record on the skeleton.
          @param stream     [out] the chunk stream whose SKID is set.
          @return nothing, or the first error. */
      Result<void> _writeSkeletonSatellites(fs::FileSystem& fs,
                                              const SatellitePaths& paths,
                                              AnimBuffers& afm2Bufs,
                                              const auto& sequences,
                                              const std::vector<std::uint32_t>& boneFdids,
                                              auto& stream) const requires (V >= M2ChunkedContainer);

      /** Write the .skin views and LOD bands, recording their FileDataIDs into
          @a stream.skinFdids (views first, then bands).
          @param fs     the filesystem gateway.
          @param paths  the satellite naming conventions.
          @param stream [out] the chunk stream whose SFID refs are rebuilt.
          @return nothing, or the first error. */
      Result<void> _writeChunkedSkins(fs::FileSystem& fs, const SatellitePaths& paths, auto& stream) const requires (V
        >= M2ChunkedContainer);

      /** Bake the .phys file (when present) and record its FileDataID into
          @a stream.physFdid.
          @param fs     the filesystem gateway.
          @param paths  the satellite naming conventions.
          @param stream [out] the chunk stream whose PFID ref is rebuilt.
          @return nothing, or the first error. */
      Result<void> _writeChunkedPhys(fs::FileSystem& fs, const SatellitePaths& paths, auto& stream) const requires (V
        >= M2ChunkedContainer);

      /** Whether the SKID reference chunk actually engages a skeleton — files
          may carry the chunk with a stored 0 meaning "none", and read and
          write MUST agree on this predicate (a mismatch would convert such a
          model into a broken skel-based one on round-trip).
          @param skeletonFdid the SKID entries.
          @return whether a skeleton is referenced. */
      static bool _skeletonEngaged(std::span<const std::uint32_t> skeletonFdid) {
        return !skeletonFdid.empty() && skeletonFdid.front() != 0;
      }
    };
  }

  /** A whole model — the canonicalizing face of detail::M2: every client
      version maps to its range's first grid version (M2AssemblyPivots),
      so one instantiation serves e.g. both Cata and MoP. */
  template <ClientVersion V>
  using M2 = detail::M2<canonicalVersion(V, M2AssemblyPivots, M2Versions)>;
}

// --- fs-level read/write definitions -----------------------------------------
// Inline in this header (not a separate io.hpp): the entities are templates,
// so the definitions must be visible for implicit instantiation anyway — the
// library ships NO explicit instantiations; every consumer TU instantiates
// exactly the versions it uses (the bindings expand the full matrix in their
// own translation units, see bindings/instantiations/).
namespace wowlib::formats::m2 {
  template <ClientVersion V>
  Result<void> detail::M2<V>::_checkHeader(std::uint32_t magic, std::uint32_t formatVersion) {
    if (magic != Md20Magic)
      return makeError(ErrorCode::FormatVersionMismatch,
                        std::format(
                          "not an MD20 model (magic {:#010x}; a Legion+ chunked " "file starts with MD21 instead)",
                          magic));
    constexpr auto range = m2FormatVersionRange(V);
    if (formatVersion < range.first || formatVersion > range.second)
      return makeError(ErrorCode::FormatVersionMismatch,
                        std::format("MD20 version {} is outside the requested client's " "era [{}, {}]", formatVersion,
                                    range.first, range.second));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_readSkinInto(fs::FileSystem& fs, const FileKey& key, std::string_view what, auto& out)
    requires (V >= M2PerSequenceTimelines) {
    const auto bytes = fs.readFile(key);
    if (!bytes)
      return makeError(bytes.error().code, std::format("{}: {}", what, bytes.error().message),
                        bytes.error().nativeError);
    Skin < V > skin;
    if (auto r = skin.read(std::span<const std::byte>{*bytes}); !r)
      return makeError(r.error().code, std::format("{}: {}", what, r.error().message), r.error().nativeError);
    if (skin.magic != SkinMagic)
      return makeError(ErrorCode::FormatVersionMismatch,
                        std::format("{} magic is {:#010x}, expected 'SKIN'", what, skin.magic));
    out.push_back(std::move(skin));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_readMonolithic(fs::FileSystem& fs, const FileKey& key, std::span<const std::byte> main)
    requires (V >= M2PerSequenceTimelines) {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return makeError(ErrorCode::PathNotResolvable,
                        "a monolithic M2's satellite files (.skin/.anim) need a " "resolvable path for the model key");
    const SatellitePaths paths{*resolved.path};

    // Low-priority sequence data lives in per-sequence .anim files; the
    // context resolves them lazily — the sequences table is populated
    // before any track member reads (layout order), so the flags are already
    // decoded when the first track consults us. A missing .anim file
    // leaves its sequences' tracks empty rather than failing.
    AnimCache cache{
      [&](SequenceKey seq) {
        return fs.readFile(FileKey{paths.anim(seq)});
      }
    };
    OffsetReadContext ctx;
    ctx.sequenceBase = cache.sequenceBase(this->root.sequences, main, AnimCache::Afm2Magic);
    if (auto r = this->root.read(main, ctx); !r) return r;
    if (auto r = _checkHeader(this->root.magic, this->root.formatVersion); !r) return r;

    this->skins.reserve(this->root.numSkinProfiles);
    for (std::uint32_t i = 0; i < this->root.numSkinProfiles; ++i)
      if (auto r = _readSkinInto(fs, FileKey{paths.skin(i)}, std::format("skin {}", i), this->skins); !r) return r;
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_readChunked(fs::FileSystem& fs, const FileKey& key, std::span<const std::byte> main)
    requires (V >= M2ChunkedContainer) {
    // 1. the chunk shell (MD21 transport blob + the reference/data chunks).
    if (auto r = this->chunks.read(main); !r) return r;

    // 2. the skeleton, when the model is skel-based — its sequences and
    //    AFID/BFID tables then stand in for the body's (which are empty).
    const bool hasSkel = _skeletonEngaged(this->chunks.skeletonFdid);
    if (hasSkel)
      if (auto r = _loadSkeleton(fs); !r) return r;

    // 3. the MD20 body out of the MD21 image; the transport blob is spent once
    //    decoded (the body's md21 span dies inside _readChunkedBody), so drop
    //    it now rather than keep a second whole-model image in memory —
    //    write() re-encodes root into a fresh stream.
    if (auto r = _readChunkedBody(fs, key, hasSkel); !r) return r;
    this->chunks.md21.bytes = {};

    // 4. the LOD views, then 5. the .bone files and 6. the (optional) physics
    //    blob — a monadic chain, so each phase runs only if the prior succeeded.
    return _readChunkedSkins(fs).and_then([&] { return _readBoneFiles(fs, hasSkel); }).transform([&] {
      _readPhysics(fs);
    });
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_loadSkeleton(fs::FileSystem& fs) requires (V >= M2ChunkedContainer) {
    if (auto r = this->skel.read(fs, FileKey{FileDataID{this->chunks.skeletonFdid.front()}}); !r)
      return makeError(r.error().code, std::format(".skel: {}", r.error().message), r.error().nativeError);
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_readChunkedBody(fs::FileSystem& fs, const FileKey& key, bool hasSkel) requires (V >=
    M2ChunkedContainer) {
    // name fallback for satellites without FileDataID entries
    std::optional<SatellitePaths> paths;
    if (const FileKey resolved = fs.resolve(key); resolved.path) paths.emplace(*resolved.path);

    const std::span<const std::byte> md21{this->chunks.md21.bytes};

    // skel-based models keep their sequences (and thus the external-data
    // flags) in the skeleton; the body's own table is empty then
    const auto* sequences = hasSkel ? &this->skel.sequenceBlock.sequences : &this->root.sequences;
    const auto& afids = hasSkel ? this->skel.effectiveAnimFdids() : this->chunks.animFdids;

    AnimCache cache{
      [&, paths](SequenceKey seq) -> Result<FileBuffer> {
        const std::uint32_t fdid = AnimCache::afidLookup(afids, seq);
        if (fdid != 0) return fs.readFile(FileKey{FileDataID{fdid}});
        if (paths) return fs.readFile(FileKey{paths->anim(seq)});
        return makeError(ErrorCode::FileNotFound, "no AFID entry and no path");
      }
    };
    OffsetReadContext ctx;
    ctx.sequenceBase = cache.sequenceBase(*sequences, md21, AnimCache::Afm2Magic);
    if (auto r = this->root.read(md21, ctx); !r) return r;
    return _checkHeader(this->root.magic, this->root.formatVersion);
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_readChunkedSkins(fs::FileSystem& fs) requires (V >= M2ChunkedContainer) {
    // the first numSkinProfiles SFID entries are the views, the rest the LOD
    // bands (real files occasionally truncate the LOD tail)
    if (this->chunks.skinFdids.size() < this->root.numSkinProfiles)
      return makeError(ErrorCode::InvalidEntityState, std::format("SFID holds {} entries, the body declares {} views",
                                                                   this->chunks.skinFdids.size(),
                                                                   this->root.numSkinProfiles));
    for (const auto [i, fdid] : std::views::enumerate(this->chunks.skinFdids) | std::views::take(
           this->root.numSkinProfiles))
      if (auto r = _readSkinInto(fs, FileKey{FileDataID{fdid}}, std::format("skin {}", i), this->skins); !r) return r;
    for (const auto [i, fdid] : std::views::enumerate(this->chunks.skinFdids) | std::views::drop(
           this->root.numSkinProfiles))
      if (fdid != 0)
        if (auto r = _readSkinInto(fs, FileKey{FileDataID{fdid}}, std::format("lod skin {}", i), this->lodSkins); !r)
          return r;
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_readBoneFiles(fs::FileSystem& fs, bool hasSkel) requires (V >= M2ChunkedContainer) {
    const auto& bfids = hasSkel ? this->skel.effectiveBoneFdids() : this->chunks.boneFdids;
    for (const auto [i, fdid] : std::views::enumerate(bfids)) {
      if (fdid == 0) continue;
      const auto bytes = fs.readFile(FileKey{FileDataID{fdid}});
      if (!bytes)
        return makeError(bytes.error().code, std::format(".bone {}: {}", i, bytes.error().message),
                          bytes.error().nativeError);
      BoneFile bone;
      if (auto r = bone.read(std::span<const std::byte>{*bytes}); !r)
        return makeError(r.error().code, std::format(".bone {}: {}", i, r.error().message), r.error().nativeError);
      this->boneFiles.push_back(std::move(bone));
    }
    return {};
  }

  template <ClientVersion V>
  void detail::M2<V>::_readPhysics(fs::FileSystem& fs) requires (V >= M2ChunkedContainer) {
    if (!this->chunks.physFdid.empty() && this->chunks.physFdid.front() != 0)
      if (auto bytes = fs.readFile(FileKey{FileDataID{this->chunks.physFdid.front()}})) this->phys.bytes =
        std::move(*bytes);
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::read(fs::FileSystem& fs, const FileKey& key) {
    const auto main = fs.readFile(key);
    if (!main) return std::unexpected{main.error()};

    root = {};
    if constexpr (V >= M2PerSequenceTimelines) this->skins.clear();
    if constexpr (V >= M2ChunkedContainer) {
      this->chunks = {};
      this->lodSkins.clear();
      this->phys = {};
      this->skel = {};
      this->boneFiles.clear();
    }

    if constexpr (V < M2PerSequenceTimelines) {
      if (auto r = root.read(std::span<const std::byte>{*main}); !r) return r;
      return _checkHeader(root.magic, root.formatVersion);
    }
    else if constexpr (V < M2ChunkedContainer) {
      return _readMonolithic(fs, key, std::span<const std::byte>{*main});
    }
    else {
      std::uint32_t lead = 0;
      if (main->size() >= 4) std::memcpy(&lead, main->data(), 4);
      if (lead == Md20Magic) {
        // Legion clients still served leftover raw MD20 models; from BfA on
        // none exist, so a bare image under a BfA+ target is a mismatch, not
        // a fallback (M2ChunkedOnly).
        if constexpr (V < M2ChunkedOnly) return _readMonolithic(fs, key, std::span<const std::byte>{*main});
        else
          return makeError(ErrorCode::FormatVersionMismatch,
                            "bare MD20 model under a BfA+ target — raw (unchunked) .m2 files "
                            "no longer exist from 8.0 on; if this is a Legion-era file, read "
                            "it with the legion target");
      }
      return _readChunked(fs, key, std::span<const std::byte>{*main});
    }
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::write(fs::FileSystem& fs, const FileKey& key) const {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return makeError(ErrorCode::PathNotResolvable, "saving an M2 needs a path for the key");
    const SatellitePaths paths{*resolved.path};

    if constexpr (V < M2PerSequenceTimelines) {
      const auto bytes = root.write();
      if (!bytes) return std::unexpected{bytes.error()};
      if (auto r = fs.addFile(*resolved.path, *bytes); !r) return std::unexpected{r.error()};
      return {};
    }
    else if constexpr (V < M2ChunkedContainer) return _writeMonolithic(fs, *resolved.path, paths);
    else return _writeChunked(fs, *resolved.path, paths);
  }

  template <ClientVersion V>
  Result<FileBuffer> detail::M2<V>::_writeBodyImage(AnimBuffers& afm2Bufs, const auto& sequences) const requires (V
    >= M2PerSequenceTimelines) {
    // Low-priority sequences split back out: every external sequence gets an
    // .anim buffer, filled as the tracks route their per-sequence blocks
    // through the sinks (empty ones still write — the client requests the file
    // whenever the flags say so).
    auto bytes = root.write(afm2Bufs.sink(sequences));
    if (!bytes) return std::unexpected{bytes.error()};

    // stamp the derived skin count into the freshly written image — the baked
    // skins vector is the source of truth (numSkinProfiles is a hidden
    // layout field, see M2Root)
    constexpr std::size_t countAt = M2Root < V > ::memberOffset("numSkinProfiles");
    const auto count = static_cast<std::uint32_t>(this->skins.size());
    std::memcpy(bytes->data() + countAt, &count, sizeof count);
    return bytes;
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_writeMonolithic(fs::FileSystem& fs,
                                                const std::string& path,
                                                const SatellitePaths& paths) const requires (V >=
    M2PerSequenceTimelines && V < M2ChunkedContainer) {
    AnimBuffers afm2Bufs;
    const auto bytes = _writeBodyImage(afm2Bufs, root.sequences);
    if (!bytes) return std::unexpected{bytes.error()};

    // pre-Legion: the body and raw .anim payloads under conventional names
    if (auto r = fs.addFile(path, *bytes); !r) return std::unexpected{r.error()};
    for (const auto& [seq, buf] : afm2Bufs.entries())
      if (auto r = fs.addFile(paths.anim(seq), buf); !r)
        return makeError(r.error().code, std::format("anim {:04}-{:02}: {}", seq.id, seq.variation, r.error().message),
                          r.error().nativeError);
    for (const auto [i, skin] : std::views::enumerate(this->skins)) {
      const auto skinBytes = skin.write();
      if (!skinBytes) return std::unexpected{skinBytes.error()};
      if (auto r = fs.addFile(paths.skin(static_cast<std::uint32_t>(i)), *skinBytes); !r)
        return makeError(r.error().code, std::format("skin {}: {}", i, r.error().message), r.error().nativeError);
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_writeChunked(fs::FileSystem& fs,
                                             const std::string& path,
                                             const SatellitePaths& paths) const requires (V >= M2ChunkedContainer) {
    // the SAME engagement predicate the read path uses — a stored SKID of 0
    // must not flip a non-skel model into a skel-based write; a skel-based
    // model's sequence table (which drives the .anim split) lives in the skel
    const bool hasSkel = _skeletonEngaged(this->chunks.skeletonFdid);

    AnimBuffers afm2Bufs;
    auto bytes = hasSkel
                   ? _writeBodyImage(afm2Bufs, this->skel.sequenceBlock.sequences)
                   : _writeBodyImage(afm2Bufs, root.sequences);
    if (!bytes) return std::unexpected{bytes.error()};

    // rebuild the chunk stream around the re-encoded image: satellites write
    // first so their fresh FileDataIDs land in the reference chunks
    M2ChunkedFile < V > stream = this->chunks;
    stream.md21.bytes = std::move(*bytes);

    // .bone files (their fdids land in the skeleton for skel-based models)
    const auto boneFdids = _writeBoneFiles(fs, paths);
    if (!boneFdids) return std::unexpected{boneFdids.error()};

    // .anim files + (for skel models) the .skel; each records its fdids on the
    // stream (non-skel) or the skeleton (skel), rebuilt fresh
    stream.animFdids.clear();
    if (!hasSkel) {
      if (auto r = _writePlainAnims(fs, paths, afm2Bufs, stream); !r) return r;
      stream.boneFdids = *boneFdids;
    }
    else if (auto r = _writeSkeletonSatellites(fs, paths, afm2Bufs, this->skel.sequenceBlock.sequences, *boneFdids,
                                                 stream); !r) return r;

    // the .skin views, then the physics blob, then finalize: serialize the
    // rebuilt stream and store it — a monadic chain that short-circuits on the
    // first failure.
    return _writeChunkedSkins(fs, paths, stream).and_then([&] { return _writeChunkedPhys(fs, paths, stream); }).
                                                   and_then([&] { return stream.write(); }).and_then(
                                                     [&](const FileBuffer& streamBytes) {
                                                       return fs.addFile(path, streamBytes).transform(
                                                         [](FileDataID) {});
                                                     });
  }

  template <ClientVersion V>
  Result<std::vector<std::uint32_t>>
  detail::M2<V>::_writeBoneFiles(fs::FileSystem& fs, const SatellitePaths& paths) const requires (V >=
    M2ChunkedContainer) {
    std::vector<std::uint32_t> boneFdids;
    for (const auto [i, bone] : std::views::enumerate(this->boneFiles)) {
      const auto boneBytes = bone.write();
      if (!boneBytes) return std::unexpected{boneBytes.error()};
      const auto r = fs.addFile(paths.bone(static_cast<std::uint32_t>(i)), *boneBytes);
      if (!r)
        return makeError(r.error().code, std::format(".bone {}: {}", i, r.error().message), r.error().nativeError);
      boneFdids.push_back(r->value);
    }
    return boneFdids;
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_writePlainAnims(fs::FileSystem& fs,
                                                 const SatellitePaths& paths,
                                                 AnimBuffers& afm2Bufs,
                                                 auto& stream) const requires (V >= M2ChunkedContainer) {
    // .anim payloads, AFM2-wrapped when the model asks for chunked ones
    for (const auto& [seq, buf] : afm2Bufs.entries()) {
      FileBuffer file;
      if (hasFlag(root.globalFlags, GlobalFlags::ChunkedAnimFiles)) afm2Bufs.appendChunkTo(
        file, AnimCache::Afm2Magic, seq);
      else file = buf;
      const auto r = fs.addFile(paths.anim(seq), file);
      if (!r)
        return makeError(r.error().code, std::format("anim {:04}-{:02}: {}", seq.id, seq.variation, r.error().message),
                          r.error().nativeError);
      stream.animFdids.push_back({seq.id, seq.variation, r->value});
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_writeSkeletonSatellites(fs::FileSystem& fs,
                                                         const SatellitePaths& paths,
                                                         AnimBuffers& afm2Bufs,
                                                         const auto& sequences,
                                                         const std::vector<std::uint32_t>& boneFdids,
                                                         auto& stream) const requires (V >= M2ChunkedContainer) {
    // re-encode the skeleton's bone/attachment blocks (splitting AFSB/AFSA per
    // sequence), then assemble the shared .anim files as AFM2 (body events) +
    // AFSA (attachments) + AFSB (bones) and hang every satellite id off the
    // skeleton.
    // deduced: inside m2::detail the bare Skeleton names the RAW template, but
    // the member is the collapsed m2::Skeleton alias type
    auto skelCopy = this->skel;
    AnimBuffers afsaBufs;
    AnimBuffers afsbBufs;
    {
      auto encoded = this->skel.boneBlock.write(afsbBufs.sink(sequences));
      if (!encoded) return std::unexpected{encoded.error()};
      skelCopy.skb1.bytes = std::move(*encoded);
      auto attachments = this->skel.attachmentBlock.write(afsaBufs.sink(sequences));
      if (!attachments) return std::unexpected{attachments.error()};
      skelCopy.ska1.bytes = std::move(*attachments);
    }

    skelCopy.animFdids.clear();
    for (const SequenceKey seq : AnimBuffers::mergedKeys({&afm2Bufs, &afsaBufs, &afsbBufs})) {
      FileBuffer file;
      afm2Bufs.appendChunkTo(file, AnimCache::Afm2Magic, seq);
      afsaBufs.appendChunkTo(file, AnimCache::AfsaMagic, seq);
      afsbBufs.appendChunkTo(file, AnimCache::AfsbMagic, seq);
      const auto r = fs.addFile(paths.anim(seq), file);
      if (!r)
        return makeError(r.error().code, std::format("anim {:04}-{:02}: {}", seq.id, seq.variation, r.error().message),
                          r.error().nativeError);
      skelCopy.animFdids.push_back({seq.id, seq.variation, r->value});
    }
    skelCopy.boneFdids = boneFdids;

    const auto skelBytes = skelCopy.template ChunkedFile<m2::Skeleton<V>>::write();
    if (!skelBytes) return std::unexpected{skelBytes.error()};
    const auto r = fs.addFile(paths.skel(), *skelBytes);
    if (!r)
      return makeError(r.error().code, std::format(".skel: {}", r.error().message), r.error().nativeError);
    stream.skeletonFdid.assign(1, r->value);
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_writeChunkedSkins(fs::FileSystem& fs, const SatellitePaths& paths, auto& stream) const
    requires (V >= M2ChunkedContainer) {
    stream.skinFdids.clear();
    for (const auto [i, skin] : std::views::enumerate(this->skins)) {
      const auto skinBytes = skin.write();
      if (!skinBytes) return std::unexpected{skinBytes.error()};
      const auto r = fs.addFile(paths.skin(static_cast<std::uint32_t>(i)), *skinBytes);
      if (!r)
        return makeError(r.error().code, std::format("skin {}: {}", i, r.error().message), r.error().nativeError);
      stream.skinFdids.push_back(r->value);
    }
    for (const auto [i, skin] : std::views::enumerate(this->lodSkins)) {
      const auto skinBytes = skin.write();
      if (!skinBytes) return std::unexpected{skinBytes.error()};
      const auto r = fs.addFile(paths.lodSkin(static_cast<std::uint32_t>(i) + 1), *skinBytes);
      if (!r)
        return makeError(r.error().code, std::format("lod skin {}: {}", i, r.error().message), r.error().nativeError);
      stream.skinFdids.push_back(r->value);
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::_writeChunkedPhys(fs::FileSystem& fs, const SatellitePaths& paths, auto& stream) const
    requires (V >= M2ChunkedContainer) {
    stream.physFdid.clear();
    if (!this->phys.bytes.empty()) {
      const auto r = fs.addFile(paths.phys(), this->phys.bytes);
      if (!r)
        return makeError(r.error().code, std::format(".phys: {}", r.error().message), r.error().nativeError);
      stream.physFdid.push_back(r->value);
    }
    return {};
  }

  namespace detail {
    /** Check one skin profile's references INTO the model body — the half of a
        skin's contracts that needs both files: the local->global vertex lookup
        and the lookup-table slices every render batch and submesh addresses.
        A profile's self-contained contracts (submesh ranges, batch submesh
        indices) are its own validateExtra.
        @param profile the LOD view's tables.
        @param root    the model body the profile refers into.
        @param report  the report findings land in. */
    template <typename Profile, typename Root>
    void validateProfileAgainstRoot(const Profile& profile, const Root& root, ValidationReport& report) {
      formats::detail::validateIndexElements(profile.vertices, root.vertices.size(), "vertices", "the model vertices",
                                               report);

      for (std::size_t i = 0; i < profile.submeshes.size() && !report.full(); ++i) {
        const auto& submesh = profile.submeshes[i];
        if (std::size_t{submesh.boneComboIndex} + submesh.boneCount > root.boneLookupTable.size())
          report.addError(std::format("submeshes[{}]", i),
                           std::format("bone-lookup range [{}, {}) overruns the {} entries", submesh.boneComboIndex,
                                       submesh.boneComboIndex + submesh.boneCount, root.boneLookupTable.size()));
      }

      for (std::size_t i = 0; i < profile.batches.size() && !report.full(); ++i) {
        const auto& batch = profile.batches[i];
        const std::string path = std::format("batches[{}]", i);
        if (batch.materialIndex >= root.materials.size())
          report.addError(path, std::format("material_index {} out of range: {} materials", batch.materialIndex,
                                             root.materials.size()));
        if (!formats::detail::isNoIndex(batch.colorIndex) && batch.colorIndex >= root.colors.size())
          report.addError(path, std::format("color_index {} out of range: {} colors", batch.colorIndex,
                                             root.colors.size()));

        // A batch addresses `textureCount` consecutive TEXTURE-lookup entries.
        // The weight and transform lookups do NOT follow textureCount — real
        // files routinely declare a longer run than those tables hold (a 3.3.5a
        // humanmale batch spans 6 over a 2-entry transparency table, the
        // Northrend glue screen 2 over 10 from index 9), so only their starting
        // entry is validated, and only when the table exists at all.
        if (std::size_t{batch.textureComboIndex} + batch.textureCount > root.textureLookupTable.size())
          report.addError(path, std::format("texture-lookup range [{}, {}) overruns the {} entries",
                                             batch.textureComboIndex, batch.textureComboIndex + batch.textureCount,
                                             root.textureLookupTable.size()));
        const auto lookupStart = [&](std::uint16_t first, const auto& table, std::string_view what) {
          if (!table.empty() && first >= table.size())
            report.addError(path, std::format("{} start {} out of range: {} entries", what, first, table.size()));
        };
        lookupStart(batch.textureWeightComboIndex, root.transparencyLookupTable, "transparency-lookup");
        lookupStart(batch.textureTransformComboIndex, root.textureTransformsLookupTable,
                     "texture-transform-lookup");
      }
    }
  }

  template <ClientVersion V>
  ValidationReport detail::M2<V>::validate() const {
    ValidationReport report;
    {
      const std::size_t mark = report.size();
      formats::detail::validateEntity(root, report);
      report.prefixFrom(mark, "root");
    }

    // The bone lookups address whichever list actually supplies the bones: a
    // skel-based model (Legion+) leaves root.bones empty and keeps them in the
    // .skel, so only the assembly can resolve this.
    {
      const auto* bones = &root.bones;
      if constexpr (requires { this->skel.boneBlock.bones; })
        if (root.bones.empty() && !this->skel.boneBlock.bones.empty()) bones = &this->skel.boneBlock.bones;
      formats::detail::validateOptionalIndexElements(root.boneLookupTable, bones->size(), "root.bone_lookup_table",
                                                        "the model bones", report);
      formats::detail::validateOptionalIndexElements(root.keyBoneLookup, bones->size(), "root.key_bone_lookup",
                                                        "the model bones", report);
    }

    // the LOD views: external .skin files WotLK+, embedded profiles before
    if constexpr (requires { this->skins; }) {
      const auto walk = [&](const auto& views, std::string_view what) {
        for (std::size_t i = 0; i < views.size() && !report.full(); ++i) {
          const std::size_t mark = report.size();
          formats::detail::validateEntity(views[i], report);
          {
            // the cross-file findings sit on the same member the entity walk
            // reports under, so they read "skins[0].profile.vertices" too
            const std::size_t profileMark = report.size();
            detail::validateProfileAgainstRoot(views[i].profile, root, report);
            report.prefixFrom(profileMark, "profile");
          }
          report.prefixFrom(mark, std::format("{}[{}]", what, i));
        }
      };
      walk(this->skins, "skins");
      if constexpr (requires { this->lodSkins; }) // the LOD bands are Legion+
        walk(this->lodSkins, "lod_skins");
    }
    else {
      for (std::size_t i = 0; i < root.skinProfiles.size() && !report.full(); ++i) {
        const std::size_t mark = report.size();
        detail::validateProfileAgainstRoot(root.skinProfiles[i], root, report);
        report.prefixFrom(mark, std::format("root.skin_profiles[{}]", i));
      }
    }
    return report;
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::ensureValid() const {
    return validate().toResult();
  }
}

// There are NO welded per-range alias tables, extern-template declarations or
// explicit instantiations here: C++ consumer TUs implicitly instantiate
// exactly the versions they use (the read/write definitions live in this
// header and the serializer engines). The language bindings,
// which weld and expand the FULL version matrix, declare the range alias
// tables and the instantiation matrix in their own translation units — see
// bindings/instantiations/m2_ranges.hpp and m2_matrix.inl.
