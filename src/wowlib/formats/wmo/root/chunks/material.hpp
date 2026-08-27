#pragma once

/** @file
    WMO materials and UV animation (MOMT, MOUV) (namespace wowlib::formats::wmo::root::chunks). */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::root::chunks {
  // --- MOMT / MOUV ------------------------------------------------------------

  enum class [[
      =welder::weld,
      =welder::doc("Material flag bits (SMOMaterial.flags).")
    ]] MaterialFlags : std::uint32_t {
    Unlit [[=welder::doc("Disable lighting.")]] = 0x1,
    Unfogged [[=welder::doc("Disable fog.")]] = 0x2,
    TwoSided [[=welder::doc("Disable backface culling.")]] = 0x4,
    ExtLight [[=welder::doc("Use exterior lighting on interior surfaces.")]] = 0x8,
    Sidn [[=welder::doc("Self-illuminated at day and night (window glow).")]] = 0x10,
    Window [[=welder::doc("A window (lighting special case).")]] = 0x20,
    ClampS [[=welder::doc("Clamp texture addressing on S.")]] = 0x40,
    ClampT [[=welder::doc("Clamp texture addressing on T.")]] = 0x80
  };

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MOMT material, 64 bytes. The layout is stable across v17; what
        changed in 8.1 is the meaning of the texture fields - MOTX byte offsets
        before (and in the fallback mode signalled by MOTX's presence),
        FileDataIDs after.)")
    ]] SMOMaterial {
    [[=welder::doc("Render flags; MaterialFlags bits.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Index into the client's WMO shader table.")]]
    std::uint32_t shader = 0;

    [[=welder::doc("Blending mode (EGxBlend).")]]
    std::uint32_t blendMode = 0;

    [[=welder::doc("First texture: MOTX offset or FileDataID.")]]
    std::uint32_t texture1 = 0;

    [[=welder::doc("Self-illuminated (night glow) color.")]]
    CImVector sidnColor{};

    [[=welder::doc("Runtime sidn slot; zero on disk.")]]
    CImVector frameSidnColor{};

    [[=welder::doc("Second texture: MOTX offset or FileDataID.")]]
    std::uint32_t texture2 = 0;

    [[=welder::doc("Diffuse color.")]]
    CImVector diffColor{};

    [[=welder::doc("Foreign key into TerrainType.")]]
    std::uint32_t groundType = 0;

    [[=welder::doc("Third texture: MOTX offset or FileDataID.")]]
    std::uint32_t texture3 = 0;

    [[=welder::doc("Extra color slot (a texture FileDataID for shader 23).")]]
    std::uint32_t color2 = 0;

    [[=welder::doc("Extra flags slot (a texture FileDataID for shader 23).")]]
    std::uint32_t flags2 = 0;

    [[=welder::doc("Nulled on load; shader-23 texture FileDataIDs.")]]
    std::array<std::uint32_t, 4> runTimeData{};
  };

  static_assert(sizeof(SMOMaterial) == 0x40);

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MOUV entry (7.3+): texture-coordinate translation speeds for two of
        the material's texture layers. Same count as the materials; all-zero
        entries mean no animation.)")
    ]] UVAnimation {
    [[=welder::doc("Translation speed per animated texture layer.")]]
    std::array<C2Vector, 2> translationSpeed{};
  };

  static_assert(sizeof(UVAnimation) == 0x10);
}
