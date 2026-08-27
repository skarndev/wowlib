#pragma once

/** @file
    ADT liquid binary pieces (namespace wowlib::formats::adt::chunks): the small
    trivially-copyable records the structured liquid entities (adt/liquid.hpp)
    build on — the MH2O vertex-format enum and UV entry, and the legacy MCLQ
    vertex union / tile / flow records. The offset-driven MH2O and MCLQ payload
    layouts are decoded by MH2OData / MCLQData, not memcpy'd, so the per-chunk and
    per-instance headers live there. */

#include <bit>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::adt::chunks {
  enum class [[
      =welder::weld,
      =welder::doc(R"(
        The MH2O per-instance vertex layout (LiquidVertexFormat). Which arrays a
        liquid instance stores — heights, depths, texture coordinates — and thus
        how wowlib decodes and re-lays its vertex data.)")
    ]] LiquidVertexFormat : std::uint16_t {
    HeightDepth [[=welder::doc(
      "Case 0: a float height map and a byte depth map "
      "(the pre-WoD go-to).")]] = 0,
    HeightUv [[=welder::doc(
      "Case 1: a float height map and a UV texture-coordinate "
      "map.")]] = 1,
    DepthOnly [[=welder::doc(
      "Case 2: a byte depth map only; the surface is flat at "
      "0.0 (ocean).")]] = 2,
    HeightUvDepth [[=welder::doc(
      "Case 3: a float height map, a UV map and a byte depth "
      "map.")]] = 3
  };

  /** One MH2O UV texture-coordinate entry (two u16, divided by 8 in shaders). */
  struct [[
      =welder::weld,
      =welder::doc(
        "An MH2O liquid UV coordinate (case 1/3): x and y, each divided by 8 "
        "for shaders.")
    ]] UVMapEntry {
    [[=welder::doc("The u coordinate (divided by 8 in shaders).")]]
    std::uint16_t x = 0;
    [[=welder::doc("The v coordinate.")]]
    std::uint16_t y = 0;

    bool operator==(const UVMapEntry&) const = default;
  };

  static_assert(sizeof(UVMapEntry) == 4);

  // --- legacy MCLQ (pre-WotLK) ------------------------------------------------

  /** The magma/slime reading of an MCLQ vertex (8 bytes): two u16 texcoords and
      the shared height. Obtain one with SLVert.asMagma(). */
  struct [[
      =welder::weld,
      =welder::doc(
        "The magma/slime reading of a legacy liquid vertex (MCLQ): u16 texture "
        "coordinates s, t and the shared height. See SLVert.as_magma().")
    ]] SMVert {
    [[=welder::doc("Texture coordinate s.")]]
    std::uint16_t s = 0;
    [[=welder::doc("Texture coordinate t.")]]
    std::uint16_t t = 0;
    [[=welder::doc("Liquid surface height (shared with the water reading).")]]
    float height = 0;

    bool operator==(const SMVert&) const = default;
  };

  static_assert(sizeof(SMVert) == 8);

  /** One legacy liquid vertex (MCLQ, 8 bytes). Byte layout fixed; the reading
      (water/ocean flow bytes vs magma/slime u16 texcoords) is set by the cell's
      liquid-type flags, not stored per vertex. */
  struct [[
      =welder::weld,
      =welder::doc(R"(
        A legacy liquid vertex (MCLQ, 8 bytes). The first four bytes are flow /
        depth data for water and ocean or two u16 texture coordinates for magma /
        slime — the cell's liquid-type flags choose the reading (use as_magma()).
        The trailing float is the surface height in all readings.)")
    ]] SLVert {
    [[=welder::doc("Water: depth. Ocean: depth. Magma/slime: low byte of s.")]]
    std::uint8_t depth = 0;
    [[=welder::doc(
      "Water: flow0 percent. Ocean: foam. Magma/slime: high byte of s.")]]
    std::uint8_t flow0 = 0;
    [[=welder::doc(
      "Water: flow1 percent. Ocean: wet. Magma/slime: low byte of t.")]]
    std::uint8_t flow1 = 0;
    [[=welder::doc("Filler. Magma/slime: high byte of t.")]]
    std::uint8_t filler = 0;
    [[=welder::doc("Liquid surface height at this vertex.")]]
    float height = 0;

    [[nodiscard]]
    [[=welder::doc(
      "Reinterpret this vertex under the magma/slime reading (u16 s, t). A "
      "pure byte reinterpretation; use it only for magma/slime cells.")]]
    SMVert asMagma() const { return std::bit_cast<SMVert>(*this); }

    [[=welder::doc("Overwrite this vertex's bytes from a magma/slime reading.")]
    ]
    void setMagma(const SMVert& magma) {
      *this = std::bit_cast<SLVert>(magma);
    }

    bool operator==(const SLVert&) const = default;
  };

  static_assert(sizeof(SLVert) == 8);

  /** One legacy liquid flow vector (MCLQ SWFlowv, 40 bytes). */
  struct [[
      =welder::weld,
      =welder::doc(
        "A legacy liquid flow vector (MCLQ, 40 bytes): a sphere of influence, a "
        "direction and velocity/amplitude/frequency.")
    ]] SWFlowv {
    [[=welder::doc("The sphere of influence.")]]
    CAaSphere sphere{};
    [[=welder::doc("The flow direction.")]]
    C3Vector direction{};
    [[=welder::doc("The flow velocity.")]]
    float velocity = 0;
    [[=welder::doc("The flow amplitude.")]]
    float amplitude = 0;
    [[=welder::doc("The flow frequency.")]]
    float frequency = 0;

    bool operator==(const SWFlowv&) const = default;
  };

  static_assert(sizeof(SWFlowv) == 40);
}
