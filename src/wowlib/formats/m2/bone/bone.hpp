#pragma once

/** @file
    The .bone entity (namespace wowlib::formats::m2::bone), WoD+: a tiny chunked
    file (with a raw u32 prelude) carrying facial-pose bone offset matrices —
    one file per variant of the FacePose (808) sequence, referenced by BFID. */

#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::m2::bone {
  /** The .bone raw prelude: a version marker, always 1 so far. */
  struct [[
      =welder::weld,
      =welder::doc("The .bone file prelude: a version marker (always 1).")
    ]] BoneFilePrelude {
    [[=welder::doc("The format version; always 1 so far.")]]
    std::uint32_t version = 1;

    bool operator==(const BoneFilePrelude&) const = default;
  };

  static_assert(sizeof(BoneFilePrelude) == 4);

  /** One .bone file: which bones get facial-pose offset matrices, and the
      matrices. The layout is stable across every client that ships them, so
      the entity is not version-templated (the version constant satisfies the
      chunk framework; no chunk here is version-gated).
      @see https://wowdev.wiki/BONE */
  struct [[
      =welder::weld,
      =welder::doc(R"(
        A .bone file (WoD+): facial-pose (FacePose 808) bone offset matrices,
        one file per pose variant, referenced by BFID. See
        https://wowdev.wiki/BONE.)")
    ]] BoneFile : ChunkedFile<BoneFile> {
    static constexpr ClientVersion Version = versions::Wod;
    static constexpr FourCCEndian UnknownFourccEndian = FourCCEndian::Forward;

    [[=formats::Header,
      =welder::doc("The raw u32 prelude (version marker).")]]
    BoneFilePrelude prelude{};

    [[
      =chunk("BIDA", FourCCEndian::Forward),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc("The affected bone ids (BIDA).")]]
    std::vector<std::uint16_t> boneIds;

    [[
      =chunk("BOMT", FourCCEndian::Forward),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc("The per-bone offset matrices (BOMT), same count as "
        "bone_ids.")]]
    std::vector<C44Matrix> boneOffsetMatrices;

    bool operator==(const BoneFile&) const = default;
  };
}
