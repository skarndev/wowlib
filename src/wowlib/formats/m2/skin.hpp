#pragma once

/** @file
    The external .skin file entity (namespace wowlib::formats::m2), WotLK+:
    the 'SKIN' magic followed by one M2SkinProfile — inherited, so the
    profile's tables sit directly on the entity (skin.submeshes, not
    skin.profile.submeshes) and match the embedded pre-WotLK profiles
    member-for-member. Offsets are relative to the .skin file itself; the
    referenced vertices stay in the .m2. */

#include <array>
#include <cstdint>
#include <string_view>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/offsets.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/records/skin.hpp>

namespace wowlib::formats::m2
{
  /** The .skin leading magic, as memcpy'd from disk. */
  inline constexpr std::uint32_t skin_magic = 0x4E494B53;  // "SKIN"

  /** One external LOD view of a model ("{model}0N.skin", or SFID FileDataIDs
      in Legion+). Only exists WotLK+ — earlier clients embed the profiles in
      the MD20 header.
      @tparam V the client version this skin targets.
      @see https://wowdev.wiki/M2/.skin */
  template <ClientVersion V>
    requires (V >= m2_per_sequence_timelines)
  struct Skin : OffsetFile<Skin<V>>, records::M2SkinProfile<V>
  {
    static constexpr ClientVersion version = V;

    /** The profile members flatten in ahead of the entity's own — the wire
        order puts the magic back in front. */
    static constexpr std::array<std::string_view, 8> wire_order{
      "magic", "vertices", "indices", "bones",
      "submeshes", "batches", "bone_count_max", "shadow_batches"};

    [[=welder::doc("The leading magic, 'SKIN'.")]]
    std::uint32_t magic = skin_magic;

    bool operator==(const Skin&) const = default;
  };
}
