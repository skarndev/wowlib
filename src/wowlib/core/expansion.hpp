#pragma once

/** @file
    The Expansion enum — the coarse, enumerable version axis the scripting
    bindings key on — and its mapping onto the ClientVersion constants wowlib
    targets (`versions::`). */

#include <array>
#include <meta>
#include <optional>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>

namespace wowlib {
  enum class [[
      =welder::weld,
      =welder::doc(R"(
        The expansion a client belongs to, in release order. The enumerable
        counterpart of a full ClientVersion: versioned format classes and
        factories are keyed on it (each enumerator maps to the
        last-minor-of-major release in the versions namespace).)")
    ]] Expansion {
    Vanilla,
    Tbc,
    Wotlk,
    Cata,
    Mop,
    Wod,
    Legion,
    Bfa,
    Shadowlands,
    Dragonflight,
    TheWarWithin
  };

  namespace detail {
    /** The targeted ClientVersion of each Expansion, indexed by enumerator
        value. Must stay in enumerator order. */
    inline constexpr std::array expansion_versions{
      versions::vanilla,
      versions::tbc,
      versions::wotlk,
      versions::cata,
      versions::mop,
      versions::wod,
      versions::legion,
      versions::bfa,
      versions::shadowlands,
      versions::dragonflight,
      versions::tww
    };

    static_assert(expansion_versions.size() == std::meta::enumerators_of(^^Expansion).size(),
                  "expansion_versions must cover every Expansion enumerator");
  }

  [[=welder::weld,
    =welder::doc(
      "The last-minor-of-major client version wowlib targets for this "
      "expansion (the versions constant)."),
    =welder::returns("the matching versions constant")]]
  constexpr ClientVersion to_client_version(Expansion expansion [[=welder::doc("the expansion")]]) {
    return detail::expansion_versions[static_cast<std::size_t>(expansion)];
  }

  [[=welder::weld,
    =welder::doc(
      "The expansion whose targeted release is exactly this version. "
      "Never matches a Classic constant — Classic clients are not "
      "expansion releases; pass version.format_lineage to ask which "
      "expansion's FORMATS one uses."),
    =welder::returns("the expansion, or None if the version is not one of the "
      "versions constants")]]
  constexpr std::optional<Expansion> to_expansion(ClientVersion version [[=welder::doc("a full client version")]]) {
    for (std::size_t i = 0; i < detail::expansion_versions.size(); ++i)
      if (detail::expansion_versions[i] == version) return static_cast<Expansion>(i);
    return std::nullopt;
  }

  [[=welder::weld,
    =welder::doc(R"(
        The expansion a client version belongs to, by major version — works for
        any build, not just the targeted releases.

        This is the CONTENT axis: Cataclysm Classic 4.4.2 reports Cata, because
        that is the game it is. It is NOT the format axis — that same client
        writes War Within-era files. For formats, ask
        to_expansion(version.format_lineage) instead.)"),
    =welder::returns("the expansion, or None for unknown majors")]]
  constexpr std::optional<Expansion> expansion_of(ClientVersion version [[=welder::doc("a full client version")]]) {
    for (std::size_t i = 0; i < detail::expansion_versions.size(); ++i)
      if (detail::expansion_versions[i].major == version.major) return static_cast<Expansion>(i);
    return std::nullopt;
  }
}
