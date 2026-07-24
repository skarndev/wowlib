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
    bone_block/attachment_block once the .anim resolution is in hand
    (read(fs, key) does both). */

#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/serializer.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/records/companion.hpp>
#include <wowlib/formats/m2/records/skel.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::formats::m2
{
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
  template <ClientVersion V>
    requires (V >= m2_chunked_container)
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A shared model skeleton (.skel, Legion 7.3+): bones, attachments and
        sequences for skel-based models, shareable between models via the
        parent link. See https://wowdev.wiki/M2/.skel.)")
  ]] Skeleton : ChunkedFile<Skeleton<V>>
  {
    static constexpr ClientVersion version = V;

    [[
      =chunk("SKL1", FourCCEndian::forward),
      =welder::doc("The skeleton identity (SKL1).")]]
    records::SkelHeader<V> header_block{};

    [[
      =chunk("SKA1", FourCCEndian::forward),
      =formats::optional,
      =welder::mark::exclude,
      =welder::doc("SKA1 transport blob; decoded into attachment_block by "
                   "read(fs, key), re-encoded on write.")]]
    ChunkBlob ska1;

    [[
      =chunk("SKB1", FourCCEndian::forward),
      =formats::optional,
      =welder::mark::exclude,
      =welder::doc("SKB1 transport blob; decoded into bone_block by "
                   "read(fs, key), re-encoded on write.")]]
    ChunkBlob skb1;

    [[
      =chunk("SKS1", FourCCEndian::forward),
      =formats::optional,
      =welder::doc("The sequence tables (SKS1).")]]
    records::SkelSequences<V> sequence_block{};

    [[
      =chunk("SKPD", FourCCEndian::forward),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("The parent-skeleton link (SKPD); 0 or 1 entries.")]]
    std::vector<records::SkelParentData> parent_link;

    [[
      =chunk("AFID", FourCCEndian::forward),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".anim FileDataIDs (AFID); absent on child skeletons, "
                   "which share the parent's (see parent_anim_fdids).")]]
    std::vector<records::AnimFileEntry> anim_fdids;

    [[
      =chunk("BFID", FourCCEndian::forward),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".bone FileDataIDs (BFID); absent on child skeletons, "
                   "which share the parent's (see parent_bone_fdids).")]]
    std::vector<std::uint32_t> bone_fdids;

    // --- decoded views (no chunk of their own; SKB1/SKA1 re-encode from
    // --- these on write) ------------------------------------------------

    [[=welder::doc("The bones (decoded SKB1).")]]
    records::SkelBones<V> bone_block{};

    [[=welder::doc("The attachments (decoded SKA1).")]]
    records::SkelAttachments<V> attachment_block{};

    [[
      =welder::mark::no_reassign,
      =welder::doc("The parent's AFID entries when this skeleton is a child "
                   "(filled by read(fs, key); not part of this file).")]]
    std::vector<records::AnimFileEntry> parent_anim_fdids;

    [[
      =welder::mark::no_reassign,
      =welder::doc("The parent's BFID entries when this skeleton is a child "
                   "(filled by read(fs, key); not part of this file).")]]
    std::vector<std::uint32_t> parent_bone_fdids;

    /** The AFID set to resolve .anim files with: this file's, or the
        parent's when this skeleton is a child without its own. */
    [[=welder::doc("The effective AFID entries: own when present, else the "
                   "parent's.")]]
    const std::vector<records::AnimFileEntry>& effective_anim_fdids() const
    {
      return anim_fdids.empty() ? parent_anim_fdids : anim_fdids;
    }

    /** The BFID set to resolve .bone files with (own, else parent's). */
    [[=welder::doc("The effective BFID entries: own when present, else the "
                   "parent's.")]]
    const std::vector<std::uint32_t>& effective_bone_fdids() const
    {
      return bone_fdids.empty() ? parent_bone_fdids : bone_fdids;
    }

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Load the skeleton: chunks, the parent's shared AFID/BFID "
                   "and the .anim-resolved bone/attachment data.")]]
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key
                      [[=welder::doc("the .skel identity (path and/or FileDataID)")]]);

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Serialize the skeleton and its .anim files (AFSA/AFSB "
                   "sections) through the project overlay. A paired model's "
                   "AFM2 section is restored by the owning M2's write.")]]
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key
                       [[=welder::doc("the .skel identity; must resolve to a path")]]) const;

    // the inherited ChunkedFile read(span)/write() stay available for
    // raw chunk-level access (blobs left undecoded)
    using ChunkedFile<Skeleton<V>>::read;
    using ChunkedFile<Skeleton<V>>::write;

    bool operator==(const Skeleton&) const = default;
  };
}
