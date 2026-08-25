#pragma once

/** @file
    _fogs.wdt chunk binary structs (namespace wowlib::formats::wdt::fogs::chunks). */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wdt::fogs::chunks {
  struct [[
      =welder::weld,
      =welder::doc("One volumetric fog (VFOG, 8.0+).")
    ]] VolumetricFog {
    [[=welder::doc("Fog color as r, g, b floats; 1.0 equals 255.")]]
    C3Vector color{};

    [[=welder::doc("Radius-related intensities.")]]
    std::array<float, 3> intensity{};

    [[=welder::doc("Unknown; 0 to 5000 in shipped files.")]]
    float unk_18 = 0;

    [[=welder::doc("Position, in server coordinates.")]]
    C3Vector position{};

    [[=welder::doc("Unknown; 0 in files, set to 1.0 on loading.")]]
    float unk_28 = 0;

    [[=welder::doc("Rotation quaternion.")]]
    C4Vector rotation{};

    [[=welder::doc("Fog start radii.")]]
    std::array<float, 3> radius{};

    [[=welder::doc(
      "Animation periods, used to derive the animation coefficients.")]]
    std::array<std::int32_t, 4> animation_periods{};

    [[=welder::doc("Flags.")]]
    std::uint32_t flags = 0;

    [[=welder::doc(
      "FileDataID of the volume model (one M2Batch only); 0 falls back "
      "to spells/errorcube.m2.")]]
    std::uint32_t model_fdid = 0;

    [[=welder::doc("Fog level, 0 to 2 (inclusive against the client's "
      "volumeFogLevel setting).")]]
    std::uint32_t fog_level = 0;

    [[=welder::doc("Globally unique fog id.")]]
    std::uint32_t id = 0;
  };

  static_assert(sizeof(VolumetricFog) == 0x68);

  struct [[
      =welder::weld,
      =welder::doc(
        "One VFEX extension record (11.0+, format version 2): optional "
        "extra data for a VFOG entry, matched by fog id.")
    ]] VolumetricFogExtra {
    [[=welder::doc("Unknown; defaults to 1.")]]
    std::uint32_t unk_0 = 1;

    [[=welder::doc(
      "Unknown floats; the first three carry values, the rest are 1.")]]
    std::array<float, 16> unk_1{};

    [[=welder::doc("The VFOG entry this record extends (its id field).")]]
    std::uint32_t fog_id = 0;

    [[=welder::doc("Unknown; defaults to 0.")]]
    std::array<std::uint32_t, 6> unk_3{};
  };

  static_assert(sizeof(VolumetricFogExtra) == 0x60);
}
