#pragma once

/** @file
    Flag-testing convenience for the binary formats' bit-mask enums.

    Binary structs store flag fields as plain integers — real files carry
    combined bit values (and occasionally undocumented bits), which are not
    valid enumerators — while the named bits live in scoped enums
    (GroupFlags, MaterialFlags, ...). has_flag() bridges the two without the
    call-site std::to_underlying noise. */

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
  has_flag(std::underlying_type_t<E> value, E flag) {
    return (value & std::to_underlying(flag)) == std::to_underlying(flag);
  }
}
