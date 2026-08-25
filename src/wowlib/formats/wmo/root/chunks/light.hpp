#pragma once

/** @file
    WMO lights and their extensions (MOLT, MOLV, MNLD) (namespace wowlib::formats::wmo::root::chunks). */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::root::chunks {
  // --- MOLT / MOLV / MNLD -----------------------------------------------------

  enum class [[
      =welder::weld,
      =welder::doc("The kind of a WMO light (SMOLight.type).")
    ]] LightType : std::uint8_t {
    Omni [[=welder::doc("A point light.")]] = 0,
    Spot [[=welder::doc("A spot light.")]] = 1,
    Direct [[=welder::doc("A directional light.")]] = 2,
    Ambient [[=welder::doc("An ambient light.")]] = 3
  };

  struct [[
      =welder::weld,
      =welder::doc(
        "One MOLT light. Not rendered by classic-era clients, but baked "
        "into vertex colors by the exporter.")
    ]] SMOLight {
    [[=welder::doc("The light kind; a LightType value.")]]
    std::uint8_t type = 0;

    [[=welder::doc("Whether attenuation applies.")]]
    std::uint8_t use_atten = 0;

    /** Alignment padding; zero in client files. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 2> pad{};

    [[=welder::doc("Light color.")]]
    CImVector color{};

    [[=welder::doc("Position in model space.")]]
    C3Vector position{};

    [[=welder::doc("Intensity.")]]
    float intensity = 0;

    [[=welder::doc("Orientation; spot/direct lights only.")]]
    C4Quaternion rotation{};

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;
  };

  static_assert(sizeof(SMOLight) == 0x30);

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MOLV entry (9.1+): a directional-gradient extension to a MOLT
        light. Multiple entries may extend the same light.)")
    ]] LightExtension {
    struct [[=welder::doc(
        "One directional gradient sample: a direction (usually axis-"
        "aligned) and its value.")]] Gradient {
      [[=welder::doc("Gradient direction; usually either xy or z, remainder 0.")
      ]]
      C3Vector direction{};

      [[=welder::doc("Gradient value.")]]
      float value = 0;
    };

    [[=welder::doc("The six gradient samples.")]]
    std::array<Gradient, 6> gradients{};

    /** Alignment padding; zero in client files. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 3> pad{};

    [[=welder::doc("The MOLT light this entry extends.")]]
    std::uint8_t light_index = 0;
  };

  static_assert(sizeof(LightExtension) == 0x64);

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MNLD entry (9.0+): a Shadowlands dynamic light - point or spot -
        used for everything from torch fires to window-light projection.)")
    ]] NewLight {
    [[=welder::doc("0 = point light (sphere), 1 = spot light (cone).")]]
    std::int32_t type = 0;

    [[=welder::doc("Index of this light in the MNLD array.")]]
    std::int32_t light_index = 0;

    [[=welder::doc("0x1: blend outer and inner color; 0x2: casts shadows.")]]
    std::int32_t flags = 0;

    [[=welder::doc("The doodad set this light belongs to.")]]
    std::int32_t doodad_set = 0;

    [[=welder::doc("Inner color.")]]
    CImVector inner_color{};

    [[=welder::doc("Position in the WMO.")]]
    C3Vector position{};

    [[=welder::doc("Euler rotation in radians; rotates the light (spot) or its "
      "cookie (point).")]]
    C3Vector rotation{};

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;

    [[=welder::doc("Light intensity.")]]
    float intensity = 0;

    [[=welder::doc("Outer color; used with flag 0x1.")]]
    CImVector outer_color{};

    [[=welder::doc(
      "Gradient start distance for inner/outer blending (flag 0x1).")]]
    float blend_start = 0;

    [[=welder::doc("Gradient end distance for inner/outer blending (flag 0x1).")
    ]]
    float blend_end = 0;

    /** Empty gap in the client layout. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 4> gap_1{};

    [[=welder::doc("Flickering intensity.")]]
    float flicker_intensity = 0;

    [[=welder::doc("Flickering speed.")]]
    float flicker_speed = 0;

    [[=welder::doc("0 = off, 1 = sine curve, 2 = noise curve, 3 = noise step.")]
    ]
    std::int32_t flicker_mode = 0;

    [[=welder::doc("Unknown; only zeros observed.")]]
    C3Vector unknown_1{};

    /** Empty gap in the client layout. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 4> gap_2{};

    [[=welder::doc("FileDataID of the light cookie texture (a cube map for "
      "point lights).")]]
    std::uint32_t light_cookie_fdid = 0;

    /** Empty gap in the client layout. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 20> gap_3{};

    [[=welder::doc("Falloff exponent for spot lights.")]]
    float falloff = 0;

    [[=welder::doc("Start of the spot drop-off gradient, in radians.")]]
    float inner_angle = 0;

    [[=welder::doc("End of the spot drop-off gradient, in radians.")]]
    float outer_angle = 0;

    [[=welder::doc("Scale, as raw IEEE half-float bits; used with flag 0x2.")]]
    std::uint16_t scale_half = 0;

    [[=welder::doc("Intensity multiplier, as raw IEEE half-float bits; 0 is "
      "corrected to 1 by the client.")]]
    std::uint16_t intensity_multiplier_half = 0;

    /** Trailing fields the client does not read yet; zero in files. */
    [[=welder::mark::exclude]] std::array<std::int32_t, 11> unused{};
  };

  static_assert(sizeof(NewLight) == 0xB8);
}
