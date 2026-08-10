#pragma once

/** @file
    _lgt.wdt chunk binary structs (namespace wowlib::formats::wdt::lights::chunks):
    the WoD point lights (MPLT), their Legion replacement (MPL2), the
    Shadowlands revision (MPL3), spot lights (MSLT) and the light texture
    animations (MLTA). */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wdt::lights::chunks
{
  struct [[
    =welder::weld,
    =welder::doc("One WoD point light (MPLT); Legion clients stop reading these in "
                 "favor of MPL2.")
  ]] MapPointLightLegacy
  {
    [[=welder::doc("Unique light id within the WDT.")]]
    std::uint32_t id = 0;

    [[=welder::doc("The ADT tile x coordinate.")]]
    std::uint16_t tile_x = 0;

    [[=welder::doc("The ADT tile y coordinate.")]]
    std::uint16_t tile_y = 0;

    [[=welder::doc("Light color.")]]
    CArgb color{};

    [[=welder::doc("Light position.")]]
    C3Vector position{};

    [[=welder::doc("Intensity and related parameters; exact meaning undocumented.")]]
    std::array<float, 3> unknown{};
  };
  static_assert(sizeof(MapPointLightLegacy) == 36);

  struct [[
    =welder::weld,
    =welder::doc("One Legion point light (MPL2).")
  ]] MapPointLight
  {
    [[=welder::doc("Unique light id within the WDT; increment for each new light.")]]
    std::uint32_t id = 0;

    [[=welder::doc("Light color (b, g, r, a); alpha 0 means fully opaque.")]]
    CImVector color{};

    [[=welder::doc("Light position.")]]
    C3Vector position{};

    [[=welder::doc("Where the light's spread starts; usually 0.")]]
    float attenuation_start = 0;

    [[=welder::doc("How far the light spreads.")]]
    float attenuation_end = 0;

    [[=welder::doc("Light intensity.")]]
    float intensity = 0;

    [[=welder::doc("Rotation; usually zero for point lights.")]]
    C3Vector rotation{};

    [[=welder::doc("The ADT tile x coordinate.")]]
    std::uint16_t tile_x = 0;

    [[=welder::doc("The ADT tile y coordinate.")]]
    std::uint16_t tile_y = 0;

    [[=welder::doc("Index into MLTA, or -1 when unanimated.")]]
    std::int16_t mlta_index = -1;

    [[=welder::doc("Index into MTEX, or -1.")]]
    std::int16_t texture_index = -1;
  };
  static_assert(sizeof(MapPointLight) == 0x34);

  struct [[
    =welder::weld,
    =welder::doc("One Shadowlands point light (MPL3; replaces MPL2 in 9.0+ files).")
  ]] MapPointLight3
  {
    [[=welder::doc("Unique light id within the WDT.")]]
    std::uint32_t id = 0;

    [[=welder::doc("Light color (b, g, r, a); alpha 0 means fully opaque.")]]
    CImVector color{};

    [[=welder::doc("Light position.")]]
    C3Vector position{};

    [[=welder::doc("Where the light's spread starts.")]]
    float attenuation_start = 0;

    [[=welder::doc("How far the light spreads.")]]
    float attenuation_end = 0;

    [[=welder::doc("Light intensity.")]]
    float intensity = 0;

    [[=welder::doc("Rotation; only used to orient light-cookie projection.")]]
    C3Vector rotation{};

    [[=welder::doc("The ADT tile x coordinate.")]]
    std::uint16_t tile_x = 0;

    [[=welder::doc("The ADT tile y coordinate.")]]
    std::uint16_t tile_y = 0;

    [[=welder::doc("Index into MLTA, or -1 when unanimated.")]]
    std::int16_t mlta_index = -1;

    [[=welder::doc("Index into MTEX for the light-cookie texture, or -1.")]]
    std::int16_t texture_index = -1;

    [[=welder::doc("Flags; 0x1 casts raytraced shadows (D3D12, shadow RT level 2+).")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Scale as a raw IEEE half-float (0.5 is the default; 10.0 hides "
                   "player shadows).")]]
    std::uint16_t scale_half = 0x3800;
  };
  static_assert(sizeof(MapPointLight3) == 0x38);

  struct [[
    =welder::weld,
    =welder::doc("One Legion spot light (MSLT).")
  ]] MapSpotLight
  {
    [[=welder::doc("Unique light id within the WDT.")]]
    std::uint32_t id = 0;

    [[=welder::doc("Light color (b, g, r, a); alpha 0 means fully opaque.")]]
    CImVector color{};

    [[=welder::doc("Light position.")]]
    C3Vector position{};

    [[=welder::doc("Where the light's spread starts; must be <= attenuation_end.")]]
    float attenuation_start = 0;

    [[=welder::doc("How far the light spreads.")]]
    float attenuation_end = 0;

    [[=welder::doc("Light intensity.")]]
    float intensity = 0;

    [[=welder::doc("Rotation, radians.")]]
    C3Vector rotation{};

    [[=welder::doc("Spotlight cone radius.")]]
    float spotlight_radius = 0;

    [[=welder::doc("Inner cone angle, radians.")]]
    float inner_angle = 0;

    [[=welder::doc("Outer cone angle, radians.")]]
    float outer_angle = 0;

    [[=welder::doc("The ADT tile x coordinate.")]]
    std::uint16_t tile_x = 0;

    [[=welder::doc("The ADT tile y coordinate.")]]
    std::uint16_t tile_y = 0;

    [[=welder::doc("Index into MLTA, or -1 when unanimated.")]]
    std::int16_t mlta_index = -1;

    [[=welder::doc("Index into MTEX, or -1.")]]
    std::int16_t texture_index = -1;
  };
  static_assert(sizeof(MapSpotLight) == 0x40);

  struct [[
    =welder::weld,
    =welder::doc("One light texture animation (MLTA), referenced by mlta_index.")
  ]] LightAnimation
  {
    [[=welder::doc("Animation amplitude.")]]
    float amplitude = 0;

    [[=welder::doc("Animation frequency.")]]
    float frequency = 0;

    [[=welder::doc("Curve function: 0 off, 1 sine, 2 noise, 3 noise step.")]]
    std::int32_t function = 0;
  };
  static_assert(sizeof(LightAnimation) == 12);
}
