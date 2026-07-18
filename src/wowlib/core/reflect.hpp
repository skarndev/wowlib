#pragma once

#include <meta>
#include <string_view>
#include <type_traits>

namespace wowlib
{
  // Internal reflection utilities (not welded; C++26 <meta> based).

  /** The enumerator name of @a value, straight from reflection — no handwritten
      stringification tables anywhere in wowlib.
      @tparam E any enum type.
      @param value the enumerator.
      @return the identifier, or "<unknown>" for values outside the enumeration.
              Static storage, constexpr-usable. */
  template <typename E>
    requires std::is_enum_v<E>
  constexpr std::string_view enum_name(E value)
  {
    template for (constexpr auto e :
                  std::define_static_array(std::meta::enumerators_of(^^E)))
    {
      if (value == [:e:])
        return std::meta::identifier_of(e);
    }
    return "<unknown>";
  }
}