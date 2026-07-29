#pragma once

/** @file
    WDL per-tile chunk wire structs (namespace wowlib::formats::wdl::chunks):
    the MARE low-resolution heightmap, the MAHO hole mask and the MAOE ocean
    mask. Each appears once per present map tile. */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

namespace wowlib::formats::wdl::chunks
{
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One tile's low-resolution heightmap (a MARE payload): a 17x17 grid of
        int16 heights with a 16x16 grid of midpoints, on the same scale as the
        full-resolution ADT heights. The background mountain silhouettes are
        drawn from these.)")
  ]] TileHeights
  {
    [[=welder::doc("The 17 x 17 outer grid heights, row-major.")]]
    std::array<std::int16_t, 17 * 17> outer{};

    [[=welder::doc("The 16 x 16 inner (midpoint) heights, row-major.")]]
    std::array<std::int16_t, 16 * 16> inner{};
  };
  static_assert(sizeof(TileHeights) == 1090);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One tile's hole mask (a MAHO payload, TBC+): 16 row bitmasks, one
        per chunk row (y), one bit per chunk column (x) — a set bit is a hole.
        Blizzard sets a bit when all 16 holes of the ADT chunk are set; an
        all-zero mask is always written anyway.)")
  ]] TileHoles
  {
    [[=welder::doc("The 16 row bitmasks (y rows, x bits); a set bit is a hole.")]]
    std::array<std::uint16_t, 16> rows{};
  };
  static_assert(sizeof(TileHoles) == 32);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One tile's ocean mask (a MAOE payload, Legion+): a 32-byte mask
        selecting the ocean alpha texture — 0xFF bytes mean water everywhere,
        0x00 none. Emitted before the tile's hole mask.)")
  ]] TileOcean
  {
    [[=welder::doc("The mask bytes; 0xFF is water everywhere, 0x00 none.")]]
    std::array<std::uint8_t, 32> mask{};
  };
  static_assert(sizeof(TileOcean) == 32);
}
