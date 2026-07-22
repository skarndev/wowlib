#pragma once

/** @file
    WMO group lights and light sets (MOLP, MLSS/MLSP/MLSK, MOP2) (namespace wowlib::formats::wmo::group_chunks). */

#include <array>
#include <cstdint>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>

namespace wowlib::formats::wmo::group_chunks
{
  // --- MOLP / MLSS / MLSP / MLSK / MOP2 ---------------------------------------

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOLP point light (Legion+).")
  ]] PointLight
  {
    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_1 = 0;

    [[=welder::doc("Light color.")]]
    CImVector color{};

    [[=welder::doc("Position of the light.")]]
    C3Vector position{};

    [[=welder::doc("Intensity.")]]
    float intensity = 0;

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;

    [[=welder::doc("Unknown; only zeros observed.")]]
    float unknown_2 = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_3 = 0;

    [[=welder::doc("Unknown; possibly a color.")]]
    std::uint32_t unknown_4 = 0;
  };
  static_assert(sizeof(PointLight) == 0x2C);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One light-set range (8.1+): a (first, count) window into the group's
        spot lights (MLSS -> MOLS), point lights (MLSP -> MOLP) or point-light
        animations (MLSK -> MOP2).)")
  ]] LightSet
  {
    [[=welder::doc("First light in the referenced chunk.")]]
    std::uint32_t offset = 0;

    [[=welder::doc("Number of lights in the set.")]]
    std::uint32_t count = 0;
  };
  static_assert(sizeof(LightSet) == 0x8);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOP2 entry (8.1+): an animated point light with flicker "
                 "and light-cookie texture data.")
  ]] PointLightAnim
  {
    [[=welder::doc("Index of the light this animation belongs to.")]]
    std::uint32_t light_index = 0;

    [[=welder::doc("Light color.")]]
    CImVector color{};

    [[=welder::doc("Position of the light.")]]
    C3Vector position{};

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;

    [[=welder::doc("Intensity.")]]
    float intensity = 0;

    [[=welder::doc("Euler rotation in radians.")]]
    C3Vector rotation{};

    [[=welder::doc("Flickering intensity.")]]
    float flicker_intensity = 0;

    [[=welder::doc("Flickering speed.")]]
    float flicker_speed = 0;

    [[=welder::doc("0 = off, 1 = sine curve, 2 = noise curve, 3 = noise step.")]]
    std::int32_t flicker_mode = 0;

    /** Undeciphered leading fields of the trailing record. */
    [[=welder::mark::exclude]] std::array<std::int32_t, 4> unknown{};

    [[=welder::doc("FileDataID of the light cookie texture.")]]
    std::uint32_t light_texture_fdid = 0;

    /** Undeciphered trailing fields; only the texture id is read. */
    [[=welder::mark::exclude]] std::array<std::int32_t, 5> unknown_2{};
  };
  static_assert(sizeof(PointLightAnim) == 0x60);

  // v14-alpha-only chunks (MOLM/MOLD lightmaps, MOIN indices, the old MOLV
  // lightmap texcoords, and the never-read MPB* set) predate every supported
  // client version and are not modeled; they would round-trip through
  // ChunkExtras::unknown if ever encountered. MLSO/MOS2 exist in client
  // binaries only, never in files.
}
