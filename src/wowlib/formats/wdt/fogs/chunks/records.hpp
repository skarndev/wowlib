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
    float unk18 = 0;

    [[=welder::doc("Position, in server coordinates.")]]
    C3Vector position{};

    [[=welder::doc("Unknown; 0 in files, set to 1.0 on loading.")]]
    float unk28 = 0;

    [[=welder::doc("Rotation quaternion.")]]
    C4Vector rotation{};

    [[=welder::doc("Fog start radii.")]]
    std::array<float, 3> radius{};

    [[=welder::doc(
      "Animation periods, used to derive the animation coefficients.")]]
    std::array<std::int32_t, 4> animationPeriods{};

    [[=welder::doc("Flags.")]]
    std::uint32_t flags = 0;

    [[=welder::doc(
      "FileDataID of the volume model (one M2Batch only); 0 falls back "
      "to spells/errorcube.m2.")]]
    std::uint32_t modelFdid = 0;

    [[=welder::doc("Fog level, 0 to 2 (inclusive against the client's "
      "volumeFogLevel setting).")]]
    std::uint32_t fogLevel = 0;

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
    std::uint32_t unk0 = 1;

    [[=welder::doc(
      "Unknown floats; the first three carry values, the rest are 1.")]]
    std::array<float, 16> unk1{};

    [[=welder::doc("The VFOG entry this record extends (its id field).")]]
    std::uint32_t fogId = 0;

    [[=welder::doc("Unknown; defaults to 0.")]]
    std::array<std::uint32_t, 6> unk3{};
  };

  static_assert(sizeof(VolumetricFogExtra) == 0x60);
}
