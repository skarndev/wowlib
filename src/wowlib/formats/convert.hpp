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
    naming the versions. Identity conversion (From == To) is a copy, and so is
    any step BETWEEN versions the format canonicalizes to one range — with
    range-collapsed instantiation E<vanilla> and E<wotlk> can be the same
    type, and such steps need (and can have) no convert_step overload. */

#include <array>
#include <cstddef>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/version_range.hpp>

namespace wowlib::formats {
  /** Tags the target version in convert_step overload signatures. */
  template <ClientVersion V>
  struct version_tag {};

  /** The ordered release list of format template @a E. Specialize per
      format (for BOTH the public alias and the detail template — deduction
      from an entity reference sees the detail one):
      @code
      template <> inline constexpr auto supported_versions<wmo::WMO> = wmo::wmo_versions;
      @endcode */
  template <template <ClientVersion> class E>
  inline constexpr auto supported_versions = nullptr;

  /** The canonicalization pivots of format template @a E (the assembly's
      pivot list from its boundaries header) — convert() walks the CANONICAL
      ladder these define: versions inside one range are the same type and
      need no step. Specialize alongside supported_versions, for both alias
      and detail spellings. */
  template <template <ClientVersion> class E>
  inline constexpr auto version_pivots = nullptr;

  namespace detail {
    template <std::size_t N>
    consteval std::size_t index_of(const std::array<ClientVersion, N>& versions, ClientVersion v) {
      for (std::size_t i = 0; i < N; ++i)
        if (versions[i] == v) return i;
      return N; // not found
    }

    template <template <ClientVersion> class E, ClientVersion From, ClientVersion To> concept HasConvertStep = requires(
      const E<From>& src) {
        { convert_step(src, version_tag<To>{}) } -> std::same_as<Result<E<To>>>;
      };

    /** The next CANONICAL version on @a E's ladder walking from @a from
        toward @a to — the first range boundary crossed in that direction.
        @pre from and to are grid versions with different canonicals. */
    template <template <ClientVersion> class E>
    consteval ClientVersion
    next_canonical(ClientVersion from, ClientVersion to) {
      const auto& versions = supported_versions<E>;
      const auto& pivots = version_pivots<E>;
      std::size_t i = index_of(versions, from);
      const std::size_t j = index_of(versions, to);
      const ClientVersion from_canonical = canonical_version(from, pivots, versions);
      while (i != j) {
        i = i < j ? i + 1 : i - 1;
        const ClientVersion c = canonical_version(versions[i], pivots, versions);
        if (c != from_canonical) return c;
      }
      return from_canonical;
    }
  }

  /** Whether every convert_step along the CANONICAL ladder from @a From to
      @a To exists, i.e. whether convert<To>(E<From>) would compile. Lets
      callers that dispatch over versions at runtime (the Python factories)
      degrade a missing ladder to a runtime error instead of tripping
      convert()'s static_assert.
      @tparam E    the format template (alias or detail spelling).
      @tparam From the source client version (a supported_versions entry).
      @tparam To   the target client version (a supported_versions entry). */
  template <template <ClientVersion> class E, ClientVersion From, ClientVersion To>
  consteval bool has_convert_path() {
    constexpr auto& versions = supported_versions<E>;
    constexpr auto& pivots = version_pivots<E>;
    static_assert(detail::index_of(versions, From) < versions.size(), "From is not a supported version of this format");
    static_assert(detail::index_of(versions, To) < versions.size(), "To is not a supported version of this format");
    constexpr ClientVersion from_c = canonical_version(From, pivots, versions);
    constexpr ClientVersion to_c = canonical_version(To, pivots, versions);
    if constexpr (from_c == to_c) return true;
    else {
      constexpr ClientVersion next = detail::next_canonical<E>(from_c, to_c);
      if constexpr (detail::HasConvertStep<E, from_c, next>) return has_convert_path<E, next, To>();
      else return false;
    }
  }

  /** Convert @a src to its @a To - version representation by composing
      convert_step overloads along the format's CANONICAL ladder (one step per
      range boundary crossed; versions inside one range are the same type and
      cost nothing).
      @tparam To  the target client version (a supported_versions entry).
      @param src the source entity (a canonical instantiation — every entity
                 built through the public aliases is one).
      @return the converted entity, or the first failing step's error. */
  template <ClientVersion To, template <ClientVersion> class E, ClientVersion From> requires(!std::is_same_v<
    decltype(supported_versions<E>), const std::nullptr_t>)
  auto convert(const E<From>& src) -> Result<E<canonical_version(To, version_pivots<E>, supported_versions<E>)>> {
    constexpr auto& versions = supported_versions<E>;
    constexpr auto& pivots = version_pivots<E>;
    static_assert(detail::index_of(versions, To) < versions.size(), "To is not a supported version of this format");
    static_assert(From == canonical_version(From, pivots, versions),
                  "convert() takes a canonical instantiation — construct entities " "through the public aliases");
    constexpr ClientVersion to_c = canonical_version(To, pivots, versions);

    if constexpr (From == to_c) return src;
    else {
      constexpr ClientVersion next = detail::next_canonical<E>(From, to_c);
      static_assert(detail::HasConvertStep<E, From, next>,
                    "no convert_step overload for this canonical version pair — declare "
                    "Result<E<next>> convert_step(const E<From>&, version_tag<next>)") ;
      auto stepped = convert_step(src, version_tag<next>{});
      if (!stepped) return std::unexpected{stepped.error()};
      return convert<To>(*stepped);
    }
  }
}
