#pragma once

/** @file
    The wire-level math and color primitives shared across WoW file formats
    (wowdev.wiki Common_Types), under their established client names. All are
    trivially copyable with exact on-disk layout — chunk payloads memcpy
    straight into arrays of them. Fixed-size sequences are std::array (layout
    guarded by the size asserts) so they cross into the bindings. */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

/** Empty-base optimization for a wire struct that carries a (welded, empty)
    facade base: gcc/clang always elide it, but MSVC needs the hint once a class
    has more than one empty base — spelled here so a memcpy'd struct's on-disk
    layout is compiler-independent. wowlib compiles under gcc-16 only today
    (C++26 reflection), so this is future-proofing; the size static_asserts are
    the actual guarantee. */
#if defined(_MSC_VER)
#  define WOWLIB_EMPTY_BASES __declspec(empty_bases)
#else
#  define WOWLIB_EMPTY_BASES
#endif

namespace wowlib::formats::common
{
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 2D float vector: texture coordinates, UV animation speeds.")
  ]] C2Vector
  {
    [[=welder::doc("The x component.")]]
    float x = 0;
    [[=welder::doc("The y component.")]]
    float y = 0;

    bool operator==(const C2Vector&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 2D integer vector.")
  ]] C2iVector
  {
    [[=welder::doc("The x component.")]]
    std::int32_t x = 0;
    [[=welder::doc("The y component.")]]
    std::int32_t y = 0;

    bool operator==(const C2iVector&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 3D float vector — the workhorse: positions, normals, "
                 "rotations-as-Euler-degrees in placements.")
  ]] C3Vector
  {
    [[=welder::doc("The x component.")]]
    float x = 0;
    [[=welder::doc("The y component.")]]
    float y = 0;
    [[=welder::doc("The z component.")]]
    float z = 0;

    bool operator==(const C3Vector&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 3D integer vector.")
  ]] C3iVector
  {
    [[=welder::doc("The x component.")]]
    std::int32_t x = 0;
    [[=welder::doc("The y component.")]]
    std::int32_t y = 0;
    [[=welder::doc("The z component.")]]
    std::int32_t z = 0;

    bool operator==(const C3iVector&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 4D float vector.")
  ]] C4Vector
  {
    [[=welder::doc("The x component.")]]
    float x = 0;
    [[=welder::doc("The y component.")]]
    float y = 0;
    [[=welder::doc("The z component.")]]
    float z = 0;
    [[=welder::doc("The w component.")]]
    float w = 0;

    bool operator==(const C4Vector&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A quaternion with the scalar part LAST on disk (x, y, z, w) — note the
        difference from math libraries that lead with w.)")
  ]] C4Quaternion
  {
    [[=welder::doc("The x component.")]]
    float x = 0;
    [[=welder::doc("The y component.")]]
    float y = 0;
    [[=welder::doc("The z component.")]]
    float z = 0;
    [[=welder::doc("The w (scalar) component.")]]
    float w = 1;

    bool operator==(const C4Quaternion&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 3x3 matrix as three column C3Vectors.")
  ]] C33Matrix
  {
    [[=welder::doc("The three columns.")]]
    std::array<C3Vector, 3> columns{};

    bool operator==(const C33Matrix&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 3x4 matrix as four column C3Vectors.")
  ]] C34Matrix
  {
    [[=welder::doc("The four columns.")]]
    std::array<C3Vector, 4> columns{};

    bool operator==(const C34Matrix&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A 4x4 matrix as four column C4Vectors.")
  ]] C44Matrix
  {
    [[=welder::doc("The four columns.")]]
    std::array<C4Vector, 4> columns{};

    bool operator==(const C44Matrix&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A plane as a normal and its signed distance from the origin.")
  ]] C4Plane
  {
    [[=welder::doc("The plane normal.")]]
    C3Vector normal{};
    [[=welder::doc("The signed distance from the origin.")]]
    float distance = 0;

    bool operator==(const C4Plane&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An axis-aligned bounding box: minimum and maximum corners.")
  ]] CAaBox
  {
    [[=welder::doc("The minimum corner.")]]
    C3Vector min{};
    [[=welder::doc("The maximum corner.")]]
    C3Vector max{};

    bool operator==(const CAaBox&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An axis-aligned sphere: position and radius.")
  ]] CAaSphere
  {
    [[=welder::doc("The center position.")]]
    C3Vector position{};
    [[=welder::doc("The radius.")]]
    float radius = 0;

    bool operator==(const CAaSphere&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A float range: minimum and maximum.")
  ]] CRange
  {
    [[=welder::doc("The minimum.")]]
    float min = 0;
    [[=welder::doc("The maximum.")]]
    float max = 0;

    bool operator==(const CRange&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A color stored as r, g, b, a bytes.")
  ]] CArgb
  {
    [[=welder::doc("The red component.")]]
    std::uint8_t r = 0;
    [[=welder::doc("The green component.")]]
    std::uint8_t g = 0;
    [[=welder::doc("The blue component.")]]
    std::uint8_t b = 0;
    [[=welder::doc("The alpha component.")]]
    std::uint8_t a = 0;

    bool operator==(const CArgb&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A color stored as b, g, r, a bytes — the client's immediate-mode "
                 "vertex color layout (WMO MOCV, doodad colors).")
  ]] CImVector
  {
    [[=welder::doc("The blue component.")]]
    std::uint8_t b = 0;
    [[=welder::doc("The green component.")]]
    std::uint8_t g = 0;
    [[=welder::doc("The red component.")]]
    std::uint8_t r = 0;
    [[=welder::doc("The alpha component.")]]
    std::uint8_t a = 0;

    bool operator==(const CImVector&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A signed 16-bit fixed-point value with an implicit 0x7FFF scale.")
  ]] fixed16
  {
    [[=welder::doc("The raw fixed-point integer; value / 0x7FFF is the float.")]]
    std::int16_t value = 0;

    [[=welder::getter,
      =welder::doc("The value as a float in [-1, 1].")]]
    constexpr float as_float() const { return static_cast<float>(value) / 0x7FFF; }

    bool operator==(const fixed16&) const = default;
  };

  static_assert(sizeof(C2Vector) == 8);
  static_assert(sizeof(C2iVector) == 8);
  static_assert(sizeof(C3Vector) == 12);
  static_assert(sizeof(C3iVector) == 12);
  static_assert(sizeof(C4Vector) == 16);
  static_assert(sizeof(C4Quaternion) == 16);
  static_assert(sizeof(C33Matrix) == 36);
  static_assert(sizeof(C34Matrix) == 48);
  static_assert(sizeof(C44Matrix) == 64);
  static_assert(sizeof(C4Plane) == 16);
  static_assert(sizeof(CAaBox) == 24);
  static_assert(sizeof(CAaSphere) == 16);
  static_assert(sizeof(CRange) == 8);
  static_assert(sizeof(CArgb) == 4);
  static_assert(sizeof(CImVector) == 4);
  static_assert(sizeof(fixed16) == 2);
}

// The primitives live in wowlib::formats::common (welded as the wowlib.formats.common
// submodule), but the format code refers to them by their bare client names. This
// directive keeps both bare (C3Vector) and wowlib::formats::C3Vector lookups working —
// qualified name lookup follows using-directives — WITHOUT re-declaring them as members
// of wowlib::formats (so welder still binds them under `common`, not twice).
namespace wowlib::formats
{
  using namespace common;
}
