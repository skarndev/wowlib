#pragma once

/** @file
    Version-range canonicalization: the machinery behind collapsing a
    versioned family's instantiations to its REAL layout permutations.

    A family (record or entity template keyed on a ClientVersion NTTP)
    usually has only a handful of content permutations — a few layout pivots
    split the whole client timeline into ranges, and every version inside a
    range is the same type. Each family therefore declares its PIVOTS (the
    versions its content or serialization behavior changes at), keeps the
    annotated struct in a nested `detail` namespace, and exposes its public
    name as a CANONICALIZING alias:

        template <ClientVersion V>
        using Foo = detail::Foo<canonicalVersion(V, foo_pivots, grid)>;

    so every use-site — entity members, facade walks, user code — collapses
    to the range's first grid version automatically. The welded aliases
    become one row per range, named by rangeSuffix() ("Vanilla",
    "CataToMop", "WotlkPlus"), and rangesValid() consteval-checks each
    row table against the pivot math so the ranges can never drift.

    A pivot that does not separate two grid versions is a no-op — listing a
    boundary generously is always safe, so families include every documented
    change even when the current grid does not straddle it. */

#include <array>
#include <span>
#include <string>
#include <string_view>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/expansion.hpp>
#include <wowlib/core/reflect.hpp>

namespace wowlib::formats {
  /** The latest pivot at or below @a v — the identity of @a v's range. Two
      versions with the same floor have identical family content.

      @a v is placed on the retail timeline first (ClientVersion::formatLineage),
      because that — not the version tuple — is what a client's file layout
      follows. Only so does a Classic client land in the right range: Cataclysm
      Classic 4.4.2 is a War Within-era client, and comparing its 4.4 tuple
      against the pivots would floor it onto the Cataclysm layouts.

      @param v      the version to classify (any flavor).
      @param pivots the family's change points (any order, all retail).
      @return the floor pivot, or the zero version below every pivot. */
  constexpr ClientVersion versionFloor(ClientVersion v, std::span<const ClientVersion> pivots) {
    const ClientVersion lineage = v.formatLineage();
    ClientVersion out{0, 0, 0, 0};
    for (const ClientVersion& p : pivots)
      if (p <= lineage && out <= p) out = p;
    return out;
  }

  /** The canonical version @a v collapses to: the FIRST grid version in
      @a v's range. Only canonical versions instantiate; every other version
      aliases to its canonical (the family's welded name covers the range).
      Classic versions collapse onto the retail grid like any other (see
      @ref versionFloor), so supporting them costs no extra instantiation.
      @param v      the requested version (any flavor).
      @param pivots the family's change points.
      @param grid   the targeted release list, ascending (WmoVersions /
                    M2Versions or a family's era subset of it).
      @return the range's first grid version (the grid front when @a v
              precedes the whole grid). */
  constexpr ClientVersion canonicalVersion(ClientVersion v,
                                            std::span<const ClientVersion> pivots,
                                            std::span<const ClientVersion> grid) {
    const ClientVersion floor = versionFloor(v, pivots);
    for (const ClientVersion& g : grid)
      if (versionFloor(g, pivots) == floor) return g;
    return grid.front();
  }

  /** The suffix naming @a canonical's range on @a grid: the plain expansion
      name for a single-version range ("Wotlk"), "FirstToLast" for an
      interior range ("CataToMop"), and "FirstPlus" for a range reaching the
      grid's end ("LegionPlus") — trailing ranges grow with every new
      release, and the Plus spelling keeps their name stable when they do.
      @param canonical a canonical grid version (see canonicalVersion).
      @param pivots    the family's change points.
      @param grid      the targeted release list, ascending.
      @return the suffix, built from the Expansion enumerator spellings. */
  constexpr std::string rangeSuffix(ClientVersion canonical,
                                     std::span<const ClientVersion> pivots,
                                     std::span<const ClientVersion> grid) {
    const ClientVersion floor = versionFloor(canonical, pivots);
    ClientVersion last = canonical;
    for (const ClientVersion& g : grid)
      if (g >= canonical && versionFloor(g, pivots) == floor) last = g;
    const auto name = [](ClientVersion v) {
      return std::string{enumName(*toExpansion(v))};
    };
    if (last == canonical) return name(canonical);
    if (last == grid.back()) return name(canonical) + "Plus";
    return name(canonical) + "To" + name(last);
  }

  /** One row of a family's welded range table: the alias suffix (stringized
      from the x-macro) and the range's canonical version. */
  struct RangeRow {
    std::string_view suffix; /**< e.g. "CataToMop". */
    ClientVersion version{}; /**< the range's canonical grid version. */
  };

  /** Does @a rows exactly enumerate the family's ranges — ascending, one row
      per distinct canonical of @a grid, each named exactly as @ref
      rangeSuffix derives? The static_assert guard every family's range
      x-macro compiles against: neither a stale row nor a wrong name survives
      a pivot or grid change.
      @param rows   the family's declared rows (see RangeRow).
      @param pivots the family's change points.
      @param grid   the family's release list, ascending.
      @return whether the table is exact. */
  constexpr bool rangesValid(std::span<const RangeRow> rows,
                              std::span<const ClientVersion> pivots,
                              std::span<const ClientVersion> grid) {
    std::size_t row = 0;
    for (const ClientVersion& g : grid) {
      const ClientVersion canonical = canonicalVersion(g, pivots, grid);
      if (canonical == g) {
        if (row >= rows.size() || rows[row].version != g || rows[row].suffix != rangeSuffix(g, pivots, grid)) return
          false;
        ++row;
      }
    }
    return row == rows.size();
  }
}
