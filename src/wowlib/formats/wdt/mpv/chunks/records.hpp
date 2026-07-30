#pragma once

/** @file
    _mpv.wdt chunk binary structs (namespace wowlib::formats::wdt::mpv::chunks). */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wdt::mpv::chunks
{
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One PVPD particulate-volume point.")
  ]] ParticulatePoint
  {
    [[=welder::doc("Unknown 2D vector; components in [-1, 1].")]]
    C2Vector unk_0{};

    [[=welder::doc("Unknown; only -0.0 observed.")]]
    float unk_8 = 0;

    [[=welder::doc("Unknown.")]]
    float unk_c = 0;
  };
  static_assert(sizeof(ParticulatePoint) == 16);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One PVBD particulate-volume bounds record.")
  ]] ParticulateBounds
  {
    [[=welder::doc("Number of engaged point indices.")]]
    std::uint32_t point_count = 0;

    [[=welder::doc("The volume's bounds.")]]
    CAaBox bounds{};

    [[=welder::doc("Indices into the group's PVPD points.")]]
    std::array<std::uint32_t, 8> point_indices{};

    [[=welder::doc("Whether this entry is complete; if 0 it joins the next entry "
                   "(same bounds).")]]
    std::uint32_t complete = 1;
  };
  static_assert(sizeof(ParticulateBounds) == 0x40);
}
