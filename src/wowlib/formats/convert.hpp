#pragma once

/** @file
    Version conversion scaffolding: convert<to>() composes hand-written
    adjacent-version steps along a format's supported-version ladder.

    A format contributes:
      - a `SupportedVersions<E>` specialization (its instantiation list, in
        release order), and
      - `convert_step` overloads for each adjacent pair it can translate:
        @code
        Result<WMO<versions::cata>> convert_step(const WMO<versions::wotlk>&,
                                                 VersionTag<versions::cata>);
        @endcode

    convert<to>(src) then walks the ladder one step at a time — each step
    encodes exactly one era's format changes; N supported versions cost 2(N-1)
    steps instead of an N^2 pair matrix. A missing step is a compile-time error
    naming the versions. Identity conversion (from == to) is a copy, and so is
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
  struct VersionTag {};

  /** The ordered release list of format template @a E. Specialize per
      format (for BOTH the public alias and the detail template — deduction
      from an entity reference sees the detail one):
      @code
      template <> inline constexpr auto SupportedVersions<wmo::WMO> = wmo::WmoVersions;
      @endcode */
  template <template <ClientVersion> class E>
  inline constexpr auto SupportedVersions = nullptr;

  /** The canonicalization pivots of format template @a E (the assembly's
      pivot list from its boundaries header) — convert() walks the CANONICAL
      ladder these define: versions inside one range are the same type and
      need no step. Specialize alongside SupportedVersions, for both alias
      and detail spellings. */
  template <template <ClientVersion> class E>
  inline constexpr auto VersionPivots = nullptr;

  namespace detail {
    template <std::size_t N>
    consteval std::size_t indexOf(const std::array<ClientVersion, N>& versions, ClientVersion v) {
      for (std::size_t i = 0; i < N; ++i)
        if (versions[i] == v) return i;
      return N; // not found
    }

    template <template <ClientVersion> class E, ClientVersion from, ClientVersion to> concept HasConvertStep = requires(
      const E<from>& src) {
        { convert_step(src, VersionTag<to>{}) } -> std::same_as<Result<E<to>>>;
      };

    /** The next CANONICAL version on @a E's ladder walking from @a from
        toward @a to — the first range boundary crossed in that direction.
        @pre from and to are grid versions with different canonicals. */
    template <template <ClientVersion> class E>
    consteval ClientVersion
    nextCanonical(ClientVersion from, ClientVersion to) {
      const auto& versions = SupportedVersions<E>;
      const auto& pivots = VersionPivots<E>;
      std::size_t i = indexOf(versions, from);
      const std::size_t j = indexOf(versions, to);
      const ClientVersion fromCanonical = canonicalVersion(from, pivots, versions);
      while (i != j) {
        i = i < j ? i + 1 : i - 1;
        const ClientVersion c = canonicalVersion(versions[i], pivots, versions);
        if (c != fromCanonical) return c;
      }
      return fromCanonical;
    }
  }

  /** Whether every convert_step along the CANONICAL ladder from @a from to
      @a to exists, i.e. whether convert<to>(E<from>) would compile. Lets
      callers that dispatch over versions at runtime (the Python factories)
      degrade a missing ladder to a runtime error instead of tripping
      convert()'s static_assert.
      @tparam E    the format template (alias or detail spelling).
      @tparam from the source client version (a SupportedVersions entry).
      @tparam to   the target client version (a SupportedVersions entry). */
  template <template <ClientVersion> class E, ClientVersion from, ClientVersion to>
  consteval bool hasConvertPath() {
    constexpr auto& versions = SupportedVersions<E>;
    constexpr auto& pivots = VersionPivots<E>;
    static_assert(detail::indexOf(versions, from) < versions.size(), "From is not a supported version of this format");
    static_assert(detail::indexOf(versions, to) < versions.size(), "To is not a supported version of this format");
    constexpr ClientVersion fromC = canonicalVersion(from, pivots, versions);
    constexpr ClientVersion toC = canonicalVersion(to, pivots, versions);
    if constexpr (fromC == toC) return true;
    else {
      constexpr ClientVersion next = detail::nextCanonical<E>(fromC, toC);
      if constexpr (detail::HasConvertStep<E, fromC, next>) return hasConvertPath<E, next, to>();
      else return false;
    }
  }

  /** Convert @a src to its @a to - version representation by composing
      convert_step overloads along the format's CANONICAL ladder (one step per
      range boundary crossed; versions inside one range are the same type and
      cost nothing).
      @tparam to  the target client version (a SupportedVersions entry).
      @param src the source entity (a canonical instantiation — every entity
                 built through the public aliases is one).
      @return the converted entity, or the first failing step's error. */
  template <ClientVersion to, template <ClientVersion> class E, ClientVersion from> requires(!std::is_same_v<
    decltype(SupportedVersions<E>), const std::nullptr_t>)
  auto convert(const E<from>& src) -> Result<E<canonicalVersion(to, VersionPivots<E>, SupportedVersions<E>)>> {
    constexpr auto& versions = SupportedVersions<E>;
    constexpr auto& pivots = VersionPivots<E>;
    static_assert(detail::indexOf(versions, to) < versions.size(), "To is not a supported version of this format");
    static_assert(from == canonicalVersion(from, pivots, versions),
                  "convert() takes a canonical instantiation — construct entities " "through the public aliases");
    constexpr ClientVersion toC = canonicalVersion(to, pivots, versions);

    if constexpr (from == toC) return src;
    else {
      constexpr ClientVersion next = detail::nextCanonical<E>(from, toC);
      static_assert(detail::HasConvertStep<E, from, next>,
                    "no convert_step overload for this canonical version pair — declare "
                    "Result<E<next>> convert_step(const E<From>&, version_tag<next>)") ;
      auto stepped = convert_step(src, VersionTag<next>{});
      if (!stepped) return std::unexpected{stepped.error()};
      return convert<to>(*stepped);
    }
  }
}
