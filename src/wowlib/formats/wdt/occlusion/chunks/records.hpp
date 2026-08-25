#pragma once

/** @file
    _occ.wdt chunk binary structs (namespace wowlib::formats::wdt::occlusion::chunks). */

#include <cstdint>

#include <welder/vocabulary.hpp>

namespace wowlib::formats::wdt::occlusion::chunks {
  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MAOI record: which tile the next occlusion heightmap belongs to
        and where its 545 int16 values sit inside the MAOH payload.)")
    ]] OcclusionIndex {
    [[=welder::doc("The tile x coordinate.")]]
    std::uint16_t tile_x = 0;

    [[=welder::doc("The tile y coordinate.")]]
    std::uint16_t tile_y = 0;

    [[=welder::doc(
      "Byte offset of this tile's heightmap inside the MAOH payload. "
      "Stored as-is: MAOH round-trips verbatim, so offsets stay "
      "valid; keep them consistent when editing.")]]
    std::uint32_t offset = 0;

    [[=welder::doc(
      "Byte size of the heightmap; always (17*17 + 16*16) * 2 = 1090.")]]
    std::uint32_t size = 0;
  };

  static_assert(sizeof(OcclusionIndex) == 12);
}
