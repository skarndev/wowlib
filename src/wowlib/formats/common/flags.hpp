#pragma once

/** @file
    Flag-testing convenience for the binary formats' bit-mask enums.

    Flag fields are typed as their scoped enums (GroupFlags, MaterialFlags,
    ... — each annotated [[=welder::flags]]); real files carry OR-combined
    bit values (and occasionally undocumented bits), which are legal values
    of a fixed-underlying-type enum but not named enumerators. hasFlag()
    tests a named bit against such a value without the call-site
    std::to_underlying noise; the raw-integer overload remains for the few
    fields whose bits stay packed into wider members
    (SMODoodadDef::nameAndFlags). */

#include <type_traits>
#include <utility>

namespace wowlib::formats {
  /** Whether flag bit @a flag is set in the raw binary value @a value.
      @tparam E    the scoped flag enum (deduced).
      @param value the binary field's raw integer value.
      @param flag  the named bit to test.
      @return true when every bit of @a flag is set in @a value. */
  template <typename E> requires std::is_scoped_enum_v<E>
  [[nodiscard]] constexpr bool
  hasFlag(std::underlying_type_t<E> value, E flag) {
    return (value & std::to_underlying(flag)) == std::to_underlying(flag);
  }

  /** Whether flag bit @a flag is set in the enum-typed field value @a value.
      @tparam E    the scoped flag enum (deduced).
      @param value the field's value (possibly OR-combined, possibly carrying
                   undocumented bits).
      @param flag  the named bit to test.
      @return true when every bit of @a flag is set in @a value. */
  template <typename E> requires std::is_scoped_enum_v<E>
  [[nodiscard]] constexpr bool hasFlag(E value, E flag) {
    return (std::to_underlying(value) & std::to_underlying(flag)) ==
      std::to_underlying(flag);
  }

  /** Set (or clear) flag bit @a flag in the enum-typed field @a value.
      @tparam E    the scoped flag enum (deduced).
      @param value the field to modify.
      @param flag  the named bit to set or clear.
      @param on    true to set, false to clear. */
  template <typename E> requires std::is_scoped_enum_v<E>
  constexpr void setFlag(E& value, E flag, bool on = true) {
    value = static_cast<E>(on
                             ? std::to_underlying(value) |
                             std::to_underlying(flag)
                             : std::to_underlying(value) &
                             ~std::to_underlying(flag));
  }
}
