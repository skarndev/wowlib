#pragma once

/** @file
    WMO group info, portals and visible blocks (namespace wowlib::formats::wmo::root::chunks). */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::root::chunks
{
  // --- MOGI / MGI2 ------------------------------------------------------------

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOGI entry: per-group flags, bounds and name, mirrored from "
                 "the group file's own header.")
  ]] SMOGroupInfo
  {
    [[=welder::doc("Group flags; the same GroupFlags bits as MOGP.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Group bounding box.")]]
    CAaBox bounding_box{};

    [[=welder::doc("Byte offset of the group name in MOGN, -1 for no name.")]]
    std::int32_t name_offset = -1;
  };
  static_assert(sizeof(SMOGroupInfo) == 0x20);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MGI2 entry (9.0+): group info v2. Same count as MOGI; when present
        it overrides the older LOD-selection logic.)")
  ]] GroupInfo2
  {
    [[=welder::doc("A copy of the group file's flags2; GroupFlags2 bits.")]]
    std::uint32_t flags2 = 0;

    [[=welder::doc("Which LOD level this group belongs to.")]]
    std::uint32_t lod_index = 0;
  };
  static_assert(sizeof(GroupInfo2) == 0x8);

  // --- MOPT / MOPR / MOPE / MOVB ----------------------------------------------

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOPT portal: a polygon plane separating two groups.")
  ]] SMOPortal
  {
    [[=welder::doc("First vertex in MOPV.")]]
    std::uint16_t start_vertex = 0;

    [[=welder::doc("Vertex count.")]]
    std::uint16_t count = 0;

    [[=welder::doc("The portal plane.")]]
    C4Plane plane{};
  };
  static_assert(sizeof(SMOPortal) == 0x14);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOPR entry: a group's reference to a portal and the group "
                 "on the other side.")
  ]] SMOPortalRef
  {
    [[=welder::doc("Index into MOPT.")]]
    std::uint16_t portal_index = 0;

    [[=welder::doc("The group on the other side of the portal.")]]
    std::uint16_t group_index = 0;

    [[=welder::doc("Which side of the portal plane this reference looks from.")]]
    std::int16_t side = 0;

    [[=welder::doc("Alignment filler; zero in client files.")]]
    std::uint16_t filler = 0;
  };
  static_assert(sizeof(SMOPortalRef) == 0x8);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOPE entry (11.1+): portal extra data; largely undeciphered.")
  ]] PortalExtra
  {
    [[=welder::doc("Index into MOPT; seems to match MOPR values.")]]
    std::uint32_t portal_index = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_1 = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_2 = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_3 = 0;
  };
  static_assert(sizeof(PortalExtra) == 0x10);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOVB visible block: a vertex range in MOVV.")
  ]] SMOVisibleBlock
  {
    [[=welder::doc("First vertex in MOVV.")]]
    std::uint16_t first_vertex = 0;

    [[=welder::doc("Vertex count.")]]
    std::uint16_t count = 0;
  };
  static_assert(sizeof(SMOVisibleBlock) == 0x4);

}
