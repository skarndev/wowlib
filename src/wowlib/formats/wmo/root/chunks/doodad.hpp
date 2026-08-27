#pragma once

/** @file
    WMO doodad sets and placements (MODS, MODD) (namespace wowlib::formats::wmo::root::chunks). */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::root::chunks {
  // --- MODS / MODD ------------------------------------------------------------

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MODS doodad set: a named range of doodad placements. Set 0
        ("Set_$DefaultGlobal") is additive and always shown; other sets are
        exclusive alternatives.)")
    ]] SMODoodadSet {
    /** The raw fixed-size name field; read through name(). */
    [[=welder::mark::exclude]] std::array<char, 20> nameBytes{};

    [[=welder::doc("First doodad instance in MODD.")]]
    std::uint32_t startIndex = 0;

    [[=welder::doc("Number of doodad instances in the set.")]]
    std::uint32_t count = 0;

    /** Alignment padding; zero in client files. */
    [[=welder::mark::exclude]] std::array<char, 4> pad{};

    [[=welder::getter("name"),
      =welder::doc("The informational set name.")]]
    [[nodiscard]]
    std::string_view name() const {
      const auto end = std::find(nameBytes.begin(), nameBytes.end(), '\0');
      return {nameBytes.data(), static_cast<std::size_t>(end - nameBytes.begin())};
    }
  };

  static_assert(sizeof(SMODoodadSet) == 0x20);

  enum class [[
      =welder::weld,
      =welder::doc("Doodad placement flag bits, packed into the high byte of "
        "SMODoodadDef.name_and_flags.")
    ]] DoodadFlags : std::uint32_t {
    NameMask [[=
      welder::doc("The low 24 bits: the MODN offset / MODI index mask.")]] =
    0x00FF'FFFF,
    AcceptProjTex [[=welder::doc("Accept projected textures.")]] = 0x0100'0000,
    InteriorLighting [[=welder::doc("Use interior lighting.")]] = 0x0200'0000
  };

  struct [[
      =welder::weld,
      =welder::doc("One MODD doodad placement: an M2 instance inside the WMO, "
        "quaternion-oriented in model space.")
    ]] SMODoodadDef {
    [[=welder::doc("Packed 24-bit MODN offset / MODI index plus DoodadFlags "
      "bits in the high byte.")]]
    std::uint32_t nameAndFlags = 0;

    [[=welder::doc("Position in WMO model space (Z-up).")]]
    C3Vector position{};

    [[=welder::doc("Orientation quaternion.")]]
    C4Quaternion orientation{};

    [[=welder::doc("Uniform scale factor.")]]
    float scale = 1;

    [[=welder::doc("Color override (BGRA); alpha below 0xFF is a MOLT index.")]]
    CImVector color{};

    [[=welder::getter,
      =welder::doc(
        "The 24-bit MODN byte offset (or MODI index) of the model name.")]]
    [[nodiscard]]
    constexpr std::uint32_t nameIndex() const {
      return nameAndFlags & std::to_underlying(DoodadFlags::NameMask);
    }
  };

  static_assert(sizeof(SMODoodadDef) == 0x28);
}
