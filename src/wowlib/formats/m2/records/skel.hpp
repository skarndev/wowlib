#pragma once

/** @file
    .skel chunk payload records (namespace wowlib::formats::m2::records):
    each SK*1 chunk is a small offset-addressed header whose M2Arrays resolve
    against the chunk's own payload (padding included), reusing the model
    record types — the .skel file is where a skel-based model's skeleton,
    attachments and sequences moved in 7.3. */

#include <array>
#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/offsets.hpp>
#include <wowlib/formats/m2/records/bone.hpp>
#include <wowlib/formats/m2/records/scene.hpp>
#include <wowlib/formats/m2/records/sequence.hpp>
#include <wowlib/formats/m2/records/track.hpp>

namespace wowlib::formats::m2::records
{
  /** The SKL1 payload: the skeleton's identity. */
  template <ClientVersion V>
  struct SkelHeader : OffsetFile<SkelHeader<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::doc("Flags; 0x100 in every file observed so far.")]]
    std::uint32_t flags = 0x100;

    [[=welder::doc("The skeleton's name.")]]
    std::string name;

    [[=welder::doc("Unknown trailing bytes; always zero so far.")]]
    std::array<std::uint8_t, 4> padding{};

    bool operator==(const SkelHeader&) const = default;
  };

  /** The SKS1 payload: the sequence set that moved out of the model. */
  template <ClientVersion V>
  struct SkelSequences : OffsetFile<SkelSequences<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::no_reassign,
      =welder::doc("Global-sequence loop lengths.")]]
    std::vector<M2Loop> global_loops;

    [[=welder::mark::no_reassign,
      =welder::doc("The animation sequences.")]]
    std::vector<M2Sequence<V>> sequences;

    [[=welder::mark::no_reassign,
      =welder::doc("Animation-id hash table (see M2Data.sequence_lookups).")]]
    std::vector<std::int16_t> sequence_lookups;

    [[=welder::doc("Unknown trailing bytes; always zero so far.")]]
    std::array<std::uint8_t, 8> padding{};

    /** Chunk engagement: emitted only when any table holds data. */
    [[=welder::mark::exclude]]
    bool empty() const
    {
      return global_loops.empty() && sequences.empty() && sequence_lookups.empty();
    }

    bool operator==(const SkelSequences&) const = default;
  };

  /** The SKB1 payload: the bones that moved out of the model. External
      sequences' track data lives in the .anim files' AFSB chunks — decode
      through the offset contexts, not straight off the chunk. */
  template <ClientVersion V>
  struct SkelBones : OffsetFile<SkelBones<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::no_reassign,
      =welder::doc("The bones.")]]
    std::vector<M2CompBone<V>> bones;

    [[=welder::mark::no_reassign,
      =welder::doc("Key-bone lookup: key bone slot -> bone index, -1 if none.")]]
    std::vector<std::int16_t> key_bone_lookup;

    [[=welder::mark::exclude]]
    bool empty() const
    {
      return bones.empty() && key_bone_lookup.empty();
    }

    bool operator==(const SkelBones&) const = default;
  };

  /** The SKA1 payload: the attachments that moved out of the model. External
      sequences' track data lives in the .anim files' AFSA chunks. */
  template <ClientVersion V>
  struct SkelAttachments : OffsetFile<SkelAttachments<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::no_reassign,
      =welder::doc("The attachment points.")]]
    std::vector<M2Attachment<V>> attachments;

    [[=welder::mark::no_reassign,
      =welder::doc("Attachment lookup: attachment id -> index.")]]
    std::vector<std::uint16_t> attachment_lookup_table;

    [[=welder::mark::exclude]]
    bool empty() const
    {
      return attachments.empty() && attachment_lookup_table.empty();
    }

    bool operator==(const SkelAttachments&) const = default;
  };

  /** The SKPD payload: the parent-skeleton link used for de-duplication
      (e.g. lightforgeddraeneimale -> draeneimale_hd; the child shares the
      parent's AFID/BFID files while keeping its own SK*1 chunks). */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The SKPD parent-skeleton link: the parent .skel FileDataID "
                 "whose AFID/BFID satellite files this skeleton shares.")
  ]] SkelParentData
  {
    std::array<std::uint8_t, 8> padding0{};
    std::uint32_t parent_skel_file_id = 0;
    std::array<std::uint8_t, 4> padding1{};

    bool operator==(const SkelParentData&) const = default;
  };
  static_assert(sizeof(SkelParentData) == 16);
}
