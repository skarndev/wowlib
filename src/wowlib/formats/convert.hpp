#pragma once

/** @file
    Version conversion scaffolding: convert<To>() composes hand-written
    adjacent-version steps along a format's supported-version ladder.

    A format contributes:
      - a `supported_versions<E>` specialization (its instantiation list, in
        release order), and
      - `convert_step` overloads for each adjacent pair it can translate:
        @code
        Result<WMO<versions::cata>> convert_step(const WMO<versions::wotlk>&,
                                                 version_tag<versions::cata>);
        @endcode

    convert<To>(src) then walks the ladder one step at a time — each step
    encodes exactly one era's format changes; N supported versions cost 2(N-1)
    steps instead of an N^2 pair matrix. A missing step is a compile-time error
    naming the versions. Identity conversion (From == To) is a copy. */

#include <array>
#include <cstddef>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::formats
{
  /** Tags the target version in convert_step overload signatures. */
  template <ClientVersion V>
  struct version_tag
  {
  };

  /** The ordered instantiation list of format template @a E. Specialize per
      format:
      @code
      template <> inline constexpr auto supported_versions<wmo::WMO> = wmo::wmo_versions;
      @endcode */
  template <template <ClientVersion> class E>
  inline constexpr auto supported_versions = nullptr;

  namespace detail
  {
    template <std::size_t N>
    consteval std::size_t index_of(const std::array<ClientVersion, N>& versions,
                                   ClientVersion v)
    {
      for (std::size_t i = 0; i < N; ++i)
        if (versions[i] == v)
          return i;
      return N;  // not found
    }

    template <template <ClientVersion> class E, ClientVersion From, ClientVersion To>
    concept HasConvertStep = requires(const E<From>& src) {
      { convert_step(src, version_tag<To>{}) } -> std::same_as<Result<E<To>>>;
    };
  }

  /** Convert @a src to its @a To - version representation by composing
      adjacent-version convert_step overloads along the format's ladder.
      @tparam To  the target client version (a supported_versions entry).
      @param src the source entity.
      @return the converted entity, or the first failing step's error. */
  template <ClientVersion To, template <ClientVersion> class E, ClientVersion From>
    requires(!std::is_same_v<decltype(supported_versions<E>), const std::nullptr_t>)
  Result<E<To>> convert(const E<From>& src)
  {
    constexpr auto& versions = supported_versions<E>;
    constexpr std::size_t from = detail::index_of(versions, From);
    constexpr std::size_t to = detail::index_of(versions, To);
    static_assert(from < versions.size(), "From is not a supported version of this format");
    static_assert(to < versions.size(), "To is not a supported version of this format");

    if constexpr (From == To)
      return src;
    else
    {
      constexpr ClientVersion next = versions[from < to ? from + 1 : from - 1];
      static_assert(detail::HasConvertStep<E, From, next>,
                    "no convert_step overload for this adjacent version pair — declare "
                    "Result<E<next>> convert_step(const E<From>&, version_tag<next>)");
      auto stepped = convert_step(src, version_tag<next>{});
      if (!stepped)
        return std::unexpected{stepped.error()};
      return convert<To>(*stepped);
    }
  }
}
