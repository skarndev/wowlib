#pragma once

/** @file
    WMO fog and ambient volumes (MFOG, MAVD/MAVG/MBVD) (namespace wowlib::formats::wmo::root::chunks). */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::root::chunks
{
  // --- MFOG / MFED ------------------------------------------------------------

  struct [[
    =welder::weld,
    =welder::doc("One MFOG entry: a fog volume with regular and under-water "
                 "settings.")
  ]] SMOFog
  {
    // C# ONLY: the nested type and the `fog` member below both style to `Fog`,
    // and C# forbids a type and a member sharing a name (CS0102) — welder
    // diagnoses it at generation. Renaming the TYPE keeps the natural property
    // spelling (`smoFog.Fog` stays the fog band). Python and Lua are unaffected:
    // there the nested type is `Fog` and the member `fog`, which do not collide.
    struct [[=welder::weld_as(wowlib::lang::cs, "FogBand"),
             =welder::doc("One fog band: end distance, start scalar and color.")]] Fog
    {
      [[=welder::doc("Distance at which visibility ceases.")]]
      float end = 0;

      [[=welder::doc("Start = end * start_scalar (0..1).")]]
      float start_scalar = 0;

      [[=welder::doc("Fog color.")]]
      CImVector color{};
    };

    [[=welder::doc("0x1: infinite radius (interior/exterior blend fog).")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Fog volume center.")]]
    C3Vector position{};

    [[=welder::doc("Inner radius (full fog).")]]
    float smaller_radius = 0;

    [[=welder::doc("Outer radius (fog starts).")]]
    float larger_radius = 0;

    [[=welder::doc("The regular fog band.")]]
    Fog fog{};

    [[=welder::doc("The under-water fog band.")]]
    Fog under_water_fog{};
  };
  static_assert(sizeof(SMOFog) == 0x30);

  struct [[
    =welder::weld,
    =welder::doc("One MFED entry (9.0+): fog extra data; same count as MFOG.")
  ]] FogExtra
  {
    [[=welder::doc("The doodad set this fog applies to.")]]
    std::uint16_t doodad_set_id = 0;

    /** Undeciphered remainder of the record. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 14> unknown{};
  };
  static_assert(sizeof(FogExtra) == 0x10);

  // --- MAVD / MAVG / MBVD -----------------------------------------------------

  struct [[
    =welder::weld,
    =welder::doc(R"(
        One ambient volume (MAVD, 8.3+) - a spherical region overriding the
        root ambient color - or a global ambient entry (MAVG), which shares
        the layout with position/start/end zeroed.)")
  ]] AmbientVolume
  {
    [[=welder::doc("Volume center (zero in MAVG).")]]
    C3Vector position{};

    [[=welder::doc("Inner radius (zero in MAVG).")]]
    float start = 0;

    [[=welder::doc("Outer radius (zero in MAVG).")]]
    float end = 0;

    [[=welder::doc("Primary ambient color; overrides the MOHD ambient.")]]
    CImVector color_1{};

    [[=welder::doc("Secondary ambient color.")]]
    CImVector color_2{};

    [[=welder::doc("Tertiary ambient color.")]]
    CImVector color_3{};

    [[=welder::doc("0x1: blend color_1 and color_3.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("The doodad set this volume applies to.")]]
    std::uint16_t doodad_set_id = 0;

    /** Undeciphered remainder of the record. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 10> unknown{};
  };
  static_assert(sizeof(AmbientVolume) == 0x30);

  struct [[
    =welder::weld,
    =welder::doc("One MBVD entry (8.3+): a box-shaped ambient volume bounded by "
                 "six planes. Only read when MAVG or MAVD is present.")
  ]] AmbientBoxVolume
  {
    [[=welder::doc("The six bounding planes (position and start combined).")]]
    std::array<C4Plane, 6> planes{};

    [[=welder::doc("Outer distance.")]]
    float end = 0;

    [[=welder::doc("Primary ambient color.")]]
    CImVector color_1{};

    [[=welder::doc("Secondary ambient color.")]]
    CImVector color_2{};

    [[=welder::doc("Tertiary ambient color.")]]
    CImVector color_3{};

    [[=welder::doc("0x1: blend color_2 and color_3.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("The doodad set this volume applies to.")]]
    std::uint16_t doodad_set_id = 0;

    /** Undeciphered remainder of the record. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 10> unknown{};
  };
  static_assert(sizeof(AmbientBoxVolume) == 0x80);

}
