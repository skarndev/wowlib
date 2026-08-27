#pragma once

/** @file
    ADT texture binary structs (namespace wowlib::formats::adt::chunks): the
    per-cell texture layer (SMLayer / MCLY), the MTEX texture flags (MTXF) and
    parameters (MTXP), the terrain-material map (MCMT), color grading (MTCG) and
    the alpha-map downscale factor (MAMP). The terrain grid element types
    (heights, normals, vertex colors) live here too. */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::adt::chunks {
  enum class [[
      =welder::weld,
      =welder::doc(
        "Per-cell texture-layer flag bits (SMLayer.flags). The low 6 bits "
        "hold the animation rotation (0x7) and speed (0x38); see "
        "SMLayer.animation_rotation()/animation_speed().")
    ]] LayerFlags : std::uint32_t {
    AnimationEnabled [[=welder::doc("Texture-coordinate animation is on.")]] = 0x40,
    Overbright [[=welder::doc("Render much brighter (used for glowing lava).")]] = 0x80,
    UseAlphaMap [[=welder::doc(
      "This layer blends through an MCAL alpha map (set on every "
      "layer after the first).")]] = 0x100,
    AlphaMapCompressed [[=welder::doc(
      "The layer's MCAL alpha map is RLE-compressed on "
      "disk. wowlib decodes it either way; on write it "
      "re-compresses when this bit is set.")]] = 0x200,
    UseCubeMapReflection [[=welder::doc(
      "Treat the ground layer as a skybox reflection "
      "(needs an MTXF entry).")]] = 0x400,
    unknown_0x800 [[=welder::doc(
      "WoD?+ with 0x1000: apply the texture-effect texture "
      "scale.")]] = 0x800,
    unknown_0x1000 [[=welder::doc("WoD?+; see 0x800.")]] = 0x1000
  };

  /** A per-cell texture layer (MCLY, 16 bytes). offsetInMCAL is derived on
      write from the layer's alpha map, so it is hidden. */
  struct [[
      =welder::weld,
      =welder::weld_as("SMLayer"),
      =welder::doc(R"(
        A terrain texture layer (MCLY, 16 bytes): which tileset texture, the
        blend/animation flags and the ground-effect link. Layers after the first
        blend through an alpha map (see MapChunk.alphaMaps). The MCAL offset is
        derived on write and not exposed.)")
    ]] SMLayer {
    [[=welder::doc(
      "The texture: an index into the tile's texture list (MTEX names, or "
      "the MDID FileDataID table since 8.1).")]]
    std::uint32_t textureId = 0;

    [[=welder::doc(
      "Flags; LayerFlags bits (the low 6 bits are animation rotation/speed).")]]
    std::uint32_t flags = 0;

    [[=welder::mark::exclude]] std::uint32_t offsetInMcal = 0;

    [[=welder::doc(
      "The ground-effect link (a GroundEffectTexture foreign key), or -1 for "
      "none (in the alpha clients, a u16 + padding).")]]
    std::int32_t effectId = -1;

    [[nodiscard]]
    [[=welder::getter, =welder::doc(
      "The animation direction in 45-degree steps (flag bits "
      "0x7).")]]
    std::uint32_t animationRotation() const { return flags & 0x7; }

    [[nodiscard]]
    [[=welder::getter, =welder::doc("The animation speed (flag bits 0x38).")]]
    std::uint32_t animationSpeed() const { return (flags >> 3) & 0x7; }

    bool operator==(const SMLayer&) const = default;
  };

  static_assert(sizeof(SMLayer) == 0x10);

  /** One MTXF texture-flags record (WotLK+): flags for the same-index MTEX
      texture. The texture_scale nibble sits in bits 4-7 (verified empirically
      against 9.2.7 Draenor MTXP data; the applied UV scale is 1/(1 << n),
      i.e. the texture appears 2^n times larger). */
  struct [[
      =welder::weld,
      =welder::weld_as("SMTextureFlags"),
      =welder::doc(R"(
        Flags for a tileset texture (MTXF, WotLK+): one per MTEX entry. Bit 0
        disables specular/height shading and uses a cube map; bits 4-7 hold the
        texture scale exponent (UVs are divided by 1 << that nibble, so the
        texture appears 2^n times larger).)")
    ]] SMTextureFlags {
    [[=welder::doc(
      "Flags: bit 0 = use cube map (no _s/_h shading); bits 4-7 = texture "
      "scale exponent (UVs are divided by 1 << n).")]]
    std::uint32_t flags = 0;

    bool operator==(const SMTextureFlags&) const = default;
  };

  static_assert(sizeof(SMTextureFlags) == 0x4);

  /** One MTXP texture-parameter record (MoP+): the MTXF flags plus the _h
      height-texture scale/offset that drive height-based blending. */
  struct [[
      =welder::weld,
      =welder::weld_as("SMTextureParams"),
      =welder::doc(R"(
        Height-blend parameters for a tileset texture (MTXP, MoP+): the same flags
        as MTXF plus the _h texture's height scale and offset. Scale 0.0 / offset
        1.0 means no _h texture is loaded.)")
    ]] SMTextureParams {
    [[=welder::doc("Flags, as in MTXF.")]]
    std::uint32_t flags = 0;

    [[=welder::doc(
      "Height scale: _h values map to [0, height_scale) as the blend height "
      "(default 0.0).")]]
    float heightScale = 0;

    [[=welder::doc("Height offset added to the scaled _h value (default 1.0).")]
    ]
    float heightOffset = 1;

    [[=welder::doc("Padding.")]]
    std::uint32_t padding = 0;

    bool operator==(const SMTextureParams&) const = default;
  };

  static_assert(sizeof(SMTextureParams) == 0x10);

  /** One MCMT terrain-material record (Cata+): a TerrainMaterial id per layer. */
  struct [[
      =welder::weld,
      =welder::weld_as("SMTerrainMaterial"),
      =welder::doc(
        "Per-layer terrain material ids (MCMT, Cata+): four TerrainMaterial "
        "foreign keys, one per MCLY layer.")
    ]] SMTerrainMaterial {
    [[=welder::doc("The four per-layer material ids.")]]
    std::array<std::uint8_t, 4> materialId{};

    bool operator==(const SMTerrainMaterial&) const = default;
  };

  static_assert(sizeof(SMTerrainMaterial) == 0x4);

  /** One MTCG color-grading record (Shadowlands+): the color-grading LUT
      FileDataIDs for the same-index diffuse texture. */
  struct [[
      =welder::weld,
      =welder::weld_as("SMTextureColorGrading"),
      =welder::doc(R"(
        Color-grading info for a tileset texture (MTCG, Shadowlands+): a start
        distance pair and the color-grading LUT / ramp FileDataIDs applied to the
        diffuse texture at the same index.)")
    ]] SMTextureColorGrading {
    [[=welder::doc(
      "Start distance (the record is used when this or _04 is non-zero).")]]
    std::uint32_t startDistance = 0;

    // C# ONLY: a leading underscore survives the PascalCase transform, and that
    // namespace is reserved for welder's generated scaffolding (`_h_*`, `_owner`),
    // so it is diagnosed at generation. The name is the client-struct offset, so
    // spell it out rather than inventing meaning we do not have.
    [[=welder::weld_as(wowlib::lang::Cs, "Unknown04"),
      =welder::doc("Unknown companion of start_distance.")]]
    std::uint32_t _04 = 0;

    [[=welder::doc("The color-grading LUT FileDataID.")]]
    std::uint32_t colorGradingFdid = 0;

    [[=welder::doc("The color-grading ramp FileDataID.")]]
    std::uint32_t colorGradingRampFdid = 0;

    bool operator==(const SMTextureColorGrading&) const = default;
  };

  static_assert(sizeof(SMTextureColorGrading) == 0x10);

  // --- terrain grid element types --------------------------------------------

  /** One MCNR normal (3 signed bytes, x Z Y; 127 == 1.0). The 9x9+8x8 grid is
      stored as a vector of these. */
  struct [[
      =welder::weld,
      =welder::doc(R"(
        A terrain vertex normal (MCNR entry, 3 signed bytes): the X, Z, Y
        components, 127 == 1.0. One per MCVT height vertex (9x9 + 8x8 = 145).)")
    ]] MCNREntry {
    [[=welder::doc(
        "The normal components, plain X, Y, Z order; 127 == 1.0, -127 == -1.0. (The often-quoted X, Z, Y layout is wrong: verified empirically against MCVT-derived gradients, wrender 2026-08.)")
    ]]
    std::array<std::int8_t, 3> normal{};

    bool operator==(const MCNREntry&) const = default;
  };

  static_assert(sizeof(MCNREntry) == 3);
}
