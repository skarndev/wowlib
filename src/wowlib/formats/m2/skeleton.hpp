#pragma once

/** @file
    The .skel skeleton entity (namespace wowlib::formats::m2), 7.3+: a
    FIRST-CLASS entity with its own filesystem read/write — skeletons are
    SHARED between models (SKPD parent links), so an M2 references its
    skeleton rather than owning it exclusively.

    A .skel is a chunked file (forward fourccs): SKL1 identity, SKS1
    sequences, SKB1 bones and SKA1 attachments (offset headers into the
    chunk), SKPD parent link, AFID/BFID satellite ids. SKB1/SKA1 reference
    external per-sequence track data in the .anim files' AFSB/AFSA chunks, so
    they travel as verbatim blobs at chunk level and decode into the typed
    boneBlock/attachmentBlock once the .anim resolution is in hand
    (read(fs, key) does both).

    The SK*1 chunk payload records live here beside the entity: each is a
    small offset-addressed header whose M2Arrays resolve against the chunk's
    own payload (padding included), reusing the body's record types. */

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/file_entity.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/m2/offset_block.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/bone.hpp>
#include <wowlib/formats/m2/root/record/scene.hpp>
#include <wowlib/formats/m2/root/record/sequence.hpp>
#include <wowlib/formats/m2/chunked/records.hpp>
#include <wowlib/formats/m2/satellites.hpp>
#include <wowlib/fs/filesystem.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>


namespace wowlib::formats::m2 {
  // --- .skel chunk payload records ------------------------------------------
  // Stable across the whole chunked era (M2ChunkPayloadPivots is empty): each
  // family canonicalizes to a single Legion instantiation.

  namespace detail {
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc("The SKL1 payload: the skeleton's identity.")
      ]] SkelHeader : M2OffsetBlock<SkelHeader<V>> {
      static constexpr ClientVersion Version = V;

      [[=welder::doc("Flags; 0x100 in every file observed so far.")]]
      std::uint32_t flags = 0x100;

      [[=welder::doc("The skeleton's name.")]]
      std::string name;

      [[=welder::doc("Unknown trailing bytes; always zero so far.")]]
      std::array<std::uint8_t, 4> padding{};

      bool operator==(const SkelHeader&) const = default;
    };

    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc("The SKS1 payload: the sequence set that moved out of the "
          "model.")
      ]] SkelSequences : M2OffsetBlock<SkelSequences<V>> {
      static constexpr ClientVersion Version = V;

      [[=welder::mark::no_reassign,
        =welder::doc("Global-sequence loop lengths.")]]
      std::vector<root::record::M2Loop> globalLoops;

      [[=welder::mark::no_reassign,
        =welder::doc("The animation sequences.")]]
      std::vector<root::record::M2Sequence<V>> sequences;

      [[=welder::mark::no_reassign,
        =welder::doc("Animation-id hash table (see M2Root.sequence_lookups).")]]
      std::vector<std::int16_t> sequenceLookups;

      [[=welder::doc("Unknown trailing bytes; always zero so far.")]]
      std::array<std::uint8_t, 8> padding{};

      /** Chunk engagement: emitted only when any table holds data. */
      [[=welder::mark::exclude]]
      bool empty() const {
        return globalLoops.empty() && sequences.empty() && sequenceLookups.empty();
      }

      bool operator==(const SkelSequences&) const = default;
    };

    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc("The SKB1 payload: the bones that moved out of the model "
          "(external sequences' track data lives in the .anim AFSB "
          "chunks).")
      ]] SkelBones : M2OffsetBlock<SkelBones<V>> {
      static constexpr ClientVersion Version = V;

      [[=welder::mark::no_reassign,
        =welder::doc("The bones.")]]
      std::vector<root::record::M2CompBone<V>> bones;

      [[=welder::mark::no_reassign,
        =welder::doc(
          "Key-bone lookup: key bone slot -> bone index, -1 if none.")]]
      std::vector<std::int16_t> keyBoneLookup;

      [[=welder::mark::exclude]]
      bool empty() const {
        return bones.empty() && keyBoneLookup.empty();
      }

      bool operator==(const SkelBones&) const = default;
    };

    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc("The SKA1 payload: the attachments that moved out of the "
          "model (external sequences' track data lives in the .anim "
          "AFSA chunks).")
      ]] SkelAttachments : M2OffsetBlock<SkelAttachments<V>> {
      static constexpr ClientVersion Version = V;

      [[=welder::mark::no_reassign,
        =welder::doc("The attachment points.")]]
      std::vector<root::record::M2Attachment<V>> attachments;

      [[=welder::mark::no_reassign,
        =welder::doc("Attachment lookup: attachment id -> index.")]]
      std::vector<std::uint16_t> attachmentLookupTable;

      [[=welder::mark::exclude]]
      bool empty() const {
        return attachments.empty() && attachmentLookupTable.empty();
      }

      bool operator==(const SkelAttachments&) const = default;
    };
  }

  /** The SKL1 payload — canonicalizing face of detail::SkelHeader. */
  template <ClientVersion V>
  using SkelHeader = detail::SkelHeader<canonicalVersion(V, M2ChunkPayloadPivots, M2ChunkedVersions)>;
  /** The SKS1 payload — canonicalizing face of detail::SkelSequences. */
  template <ClientVersion V>
  using SkelSequences = detail::SkelSequences<canonicalVersion(V, M2ChunkPayloadPivots, M2ChunkedVersions)>;
  /** The SKB1 payload — canonicalizing face of detail::SkelBones. */
  template <ClientVersion V>
  using SkelBones = detail::SkelBones<canonicalVersion(V, M2ChunkPayloadPivots, M2ChunkedVersions)>;
  /** The SKA1 payload — canonicalizing face of detail::SkelAttachments. */
  template <ClientVersion V>
  using SkelAttachments = detail::SkelAttachments<canonicalVersion(V, M2ChunkPayloadPivots, M2ChunkedVersions)>;

  /** The SKPD payload: the parent-skeleton link used for de-duplication
      (e.g. lightforgeddraeneimale -> draeneimale_hd; the child shares the
      parent's AFID/BFID files while keeping its own SK*1 chunks). */
  struct [[
      =welder::weld,
      =welder::doc("The SKPD parent-skeleton link: the parent .skel FileDataID "
        "whose AFID/BFID satellite files this skeleton shares.")
    ]] SkelParentData {
    [[=welder::doc("Unknown leading bytes; always zero so far.")]]
    std::array<std::uint8_t, 8> padding0{};
    [[=welder::doc("The parent .skel FileDataID.")]]
    std::uint32_t parentSkelFileId = 0;
    [[=welder::doc("Unknown trailing bytes; always zero so far.")]]
    std::array<std::uint8_t, 4> padding1{};

    bool operator==(const SkelParentData&) const = default;
  };

  static_assert(sizeof(SkelParentData) == 16);

  /** The version-agnostic base of every Skeleton<V> (welded as "Skeleton").

      This empty base exists ENTIRELY for the language bindings: a common
      welded supertype for the per-version Skeleton* classes (isinstance,
      Skeleton.for_version(expansion)). No role in the C++ API.

      @see https://wowdev.wiki/M2/.skel */
  struct [[
      =welder::weld,
      =welder::weld_as("Skeleton"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A shared model skeleton (.skel, Legion 7.3+), abstract over the client
        version. Construct a concrete version with
        Skeleton.for_version(expansion); the per-version Skeleton* classes are
        subclasses. See https://wowdev.wiki/M2/.skel.)")
    ]] SkeletonBase : FileEntityBase {
    bool operator==(const SkeletonBase&) const = default;
  };

  /** A shared model skeleton (.skel, 7.3+): the bones, attachments and
      sequences a skel-based model moved out of its MD20 image, plus the
      satellite ids. Load with read(fs, key) — it follows the SKPD parent
      link for shared AFID/BFID and decodes SKB1/SKA1 through the .anim
      files; the ChunkedFile read(span) alone leaves those two as raw blobs.

      Writing standalone regenerates the .skel and its .anim files with the
      AFSA/AFSB sections only — a paired model's AFM2 (event) section is
      restored by the owning M2's write, which is the full-fidelity save
      path.
      @tparam V the client version this skeleton targets.
      @see https://wowdev.wiki/M2/.skel */
  namespace detail {
    template <ClientVersion V> requires (V >= M2ChunkedContainer)
    struct [[
        =welder::weld,
        =welder::doc(R"(
        A shared model skeleton (.skel, Legion 7.3+): bones, attachments and
        sequences for skel-based models, shareable between models via the
        parent link. See https://wowdev.wiki/M2/.skel.)")
      ]] Skeleton : ChunkedFile<Skeleton<V>>, SkeletonBase {
      static constexpr ClientVersion Version = V;
      static constexpr FourCCEndian UnknownFourccEndian = FourCCEndian::Forward;

      [[
        =chunk("SKL1", FourCCEndian::Forward),
        =welder::doc("The skeleton identity (SKL1).")]]
      SkelHeader<V> headerBlock{};

      [[
        =chunk("SKA1", FourCCEndian::Forward),
        =formats::Optional,
        =welder::mark::exclude,
        =welder::doc("SKA1 transport blob; decoded into attachment_block by "
          "read(fs, key), re-encoded on write.")]]
      ChunkBlob ska1;

      [[
        =chunk("SKB1", FourCCEndian::Forward),
        =formats::Optional,
        =welder::mark::exclude,
        =welder::doc("SKB1 transport blob; decoded into bone_block by "
          "read(fs, key), re-encoded on write.")]]
      ChunkBlob skb1;

      [[
        =chunk("SKS1", FourCCEndian::Forward),
        =formats::Optional,
        =welder::doc("The sequence tables (SKS1).")]]
      SkelSequences<V> sequenceBlock{};

      [[
        =chunk("SKPD", FourCCEndian::Forward),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("The parent-skeleton link (SKPD); 0 or 1 entries.")]]
      std::vector<SkelParentData> parentLink;

      [[
        =chunk("AFID", FourCCEndian::Forward),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(".anim FileDataIDs (AFID); absent on child skeletons, "
          "which share the parent's (see parent_anim_fdids).")]]
      std::vector<chunked::record::AnimFileEntry> animFdids;

      [[
        =chunk("BFID", FourCCEndian::Forward),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(".bone FileDataIDs (BFID); absent on child skeletons, "
          "which share the parent's (see parent_bone_fdids).")]]
      std::vector<std::uint32_t> boneFdids;

      // --- decoded views (no chunk of their own; SKB1/SKA1 re-encode from
      // --- these on write) ------------------------------------------------

      [[=welder::doc("The bones (decoded SKB1).")]]
      SkelBones<V> boneBlock{};

      [[=welder::doc("The attachments (decoded SKA1).")]]
      SkelAttachments<V> attachmentBlock{};

      [[
        =welder::mark::no_reassign,
        =welder::doc("The parent's AFID entries when this skeleton is a child "
          "(filled by read(fs, key); not part of this file).")]]
      std::vector<chunked::record::AnimFileEntry> parentAnimFdids;

      [[
        =welder::mark::no_reassign,
        =welder::doc("The parent's BFID entries when this skeleton is a child "
          "(filled by read(fs, key); not part of this file).")]]
      std::vector<std::uint32_t> parentBoneFdids;

      /** The AFID set to resolve .anim files with: this file's, or the
          parent's when this skeleton is a child without its own. */
      [[=welder::doc("The effective AFID entries: own when present, else the "
        "parent's.")]]
      const std::vector<chunked::record::AnimFileEntry>&
      effectiveAnimFdids() const {
        return animFdids.empty() ? parentAnimFdids : animFdids;
      }

      /** The BFID set to resolve .bone files with (own, else parent's). */
      [[=welder::doc("The effective BFID entries: own when present, else the "
        "parent's.")]]
      const std::vector<std::uint32_t>& effectiveBoneFdids() const {
        return boneFdids.empty() ? parentBoneFdids : boneFdids;
      }

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc("Load the skeleton: chunks, the parent's shared AFID/BFID "
          "and the .anim-resolved bone/attachment data.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key [[=welder::doc("the .skel identity (path and/or FileDataID)")]]);

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc("Serialize the skeleton and its .anim files (AFSA/AFSB "
          "sections) through the project overlay. A paired model's "
          "AFM2 section is restored by the owning M2's write.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key [[=welder::doc("the .skel identity; must resolve to a path")]]) const;

      // the inherited ChunkedFile read(span)/write() stay available for
      // raw chunk-level access (blobs left undecoded)
      using ChunkedFile<Skeleton<V>>::read;
      using ChunkedFile<Skeleton<V>>::write;

      bool operator==(const Skeleton&) const = default;
    };
  }

  /** A shared model skeleton — the canonicalizing face of detail::Skeleton:
      the whole chunked era is one range (M2ChunkPayloadPivots is empty), so a
      SINGLE instantiation serves Legion through the latest release.
      Pre-Legion versions stay a substitution failure. */
  template <ClientVersion V> requires (V >= M2ChunkedContainer)
  using Skeleton = detail::Skeleton<canonicalVersion(V, M2ChunkPayloadPivots, M2ChunkedVersions)>;
}

// --- fs-level read/write definitions (inline for the same implicit-
// instantiation reason as m2.hpp's) -------------------------------------------
namespace wowlib::formats::m2 {
  template <ClientVersion V> requires (V >= M2ChunkedContainer)
  Result<void> detail::Skeleton<V>::read(fs::FileSystem& fs, const FileKey& key) {
    const auto bytes = fs.readFile(key);
    if (!bytes) return std::unexpected{bytes.error()};

    *this = Skeleton{};
    if (auto r = ChunkedFile<Skeleton<V>>::read(std::span<const std::byte>{*bytes}); !r) return r;

    // A child skeleton shares the parent's satellite ids (only the *FID
    // chunks are shared — the child keeps its own SK*1 data). One hop, per
    // every known example; a missing parent degrades to inline-only decode.
    if (!parentLink.empty() && parentLink.front().parentSkelFileId != 0)
      if (auto pbytes = fs.readFile(FileKey{FileDataID{parentLink.front().parentSkelFileId}})) {
        Skeleton<V> parent;
        if (parent.ChunkedFile<Skeleton<V>>::read(std::span<const std::byte>{*pbytes})) {
          parentAnimFdids = parent.animFdids;
          parentBoneFdids = parent.boneFdids;
        }
      }

    // name fallback for satellites without FileDataID entries
    std::optional<SatellitePaths> paths;
    if (const FileKey resolved = fs.resolve(key); resolved.path) paths.emplace(*resolved.path);

    AnimCache cache{
      [&, paths](SequenceKey seq) -> Result<FileBuffer> {
        const std::uint32_t fdid = AnimCache::afidLookup(effectiveAnimFdids(), seq);
        if (fdid != 0) return fs.readFile(FileKey{FileDataID{fdid}});
        if (paths) return fs.readFile(FileKey{paths->anim(seq)});
        return makeError(ErrorCode::FileNotFound, "no AFID entry and no path");
      }
    };

    const auto makeCtx = [&](std::span<const std::byte> inlineBase, std::uint32_t target) {
      OffsetReadContext ctx;
      ctx.sequenceBase = cache.sequenceBase(sequenceBlock.sequences, inlineBase, target);
      return ctx;
    };

    if (!skb1.bytes.empty()) {
      const std::span<const std::byte> base{skb1.bytes};
      if (auto r = boneBlock.read(base, makeCtx(base, AnimCache::AfsbMagic)); !r)
        return makeError(r.error().code, std::format("SKB1: {}", r.error().message), r.error().nativeError);
    }
    if (!ska1.bytes.empty()) {
      const std::span<const std::byte> base{ska1.bytes};
      if (auto r = attachmentBlock.read(base, makeCtx(base, AnimCache::AfsaMagic)); !r)
        return makeError(r.error().code, std::format("SKA1: {}", r.error().message), r.error().nativeError);
    }
    // the blobs stay as read: an untouched skeleton written at chunk level
    // remains byte-perfect; the fs write path re-encodes them from the
    // typed blocks.
    return {};
  }

  template <ClientVersion V> requires (V >= M2ChunkedContainer)
  Result<void> detail::Skeleton<V>::write(fs::FileSystem& fs, const FileKey& key) const {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return makeError(ErrorCode::PathNotResolvable, "saving a skeleton needs a path for the key");
    const SatellitePaths paths{*resolved.path};

    Skeleton<V> copy = *this;

    // re-encode the bone/attachment blocks, splitting external sequences
    // into per-sequence AFSB/AFSA buffers
    AnimBuffers afsaBufs;
    AnimBuffers afsbBufs;
    {
      auto encoded = boneBlock.write(afsbBufs.sink(sequenceBlock.sequences));
      if (!encoded) return std::unexpected{encoded.error()};
      copy.skb1.bytes = std::move(*encoded);
      auto attachments = attachmentBlock.write(afsaBufs.sink(sequenceBlock.sequences));
      if (!attachments) return std::unexpected{attachments.error()};
      copy.ska1.bytes = std::move(*attachments);
    }

    // the skeleton's .anim files: AFSA + AFSB sections. NOTE: a paired
    // model's AFM2 (event) section is not represented here — the owning
    // M2's write is the full-fidelity save path.
    copy.animFdids.clear();
    for (const SequenceKey seq : AnimBuffers::mergedKeys({&afsaBufs, &afsbBufs})) {
      FileBuffer file;
      afsaBufs.appendChunkTo(file, AnimCache::AfsaMagic, seq);
      afsbBufs.appendChunkTo(file, AnimCache::AfsbMagic, seq);
      const auto r = fs.addFile(paths.anim(seq), file);
      if (!r)
        return makeError(r.error().code, std::format("anim {:04}-{:02}: {}", seq.id, seq.variation, r.error().message),
                          r.error().nativeError);
      copy.animFdids.push_back({seq.id, seq.variation, r->value});
    }

    const auto bytes = copy.ChunkedFile<Skeleton<V>>::write();
    if (!bytes) return std::unexpected{bytes.error()};
    if (auto r = fs.addFile(*resolved.path, *bytes); !r) return std::unexpected{r.error()};
    return {};
  }
}
