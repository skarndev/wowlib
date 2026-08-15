#pragma once

/** @file
    @brief The version-keyed facade for the generated client-database tables —
    fully ERASED: no template is instantiated per table family.

    Every table is a class template @c F<ClientVersion> (e.g. @c db::tables::Map)
    whose family base (@c Map_, welded as @c Map) now derives the ONE welded
    @c TableBase — so the whole method surface is inherited, and everything this
    facade adds (@c for_version overloads, the runtime fallback, the @c AnyX
    union) needs nothing but RUNTIME data: the grid, the pivots, and the
    registered class OBJECTS, which it looks up by their range-suffixed names —
    the same spelling @c concrete_name derives and dbdgen emits. Per family the
    generated shard contributes one CALL to @ref def_table_facade; per module
    the closures here instantiate once.

    The gate difference from the format facade stands: a table alias @c F<V> is
    TOTAL (it canonicalizes every version into its grid), so @c for_version is
    gated on GRID membership — @c Map.for_version(Expansion.Cata) on a table
    with no Cata block raises, rather than silently collapsing Cata onto the
    wotlk range. */

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nanobind/nanobind.h>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/expansion.hpp>

#include "facade.hpp" // FamilyEra, concrete_name, persist, expansion_values, ...

namespace wowlib_py::db
{
  namespace nb = nanobind;

  /** Whether @p v is one of the eras @p grid actually carries a layout for. */
  inline bool grid_has(wowlib::ClientVersion v,
                       std::span<const wowlib::ClientVersion> grid)
  {
    for (const wowlib::ClientVersion& g : grid)
      if (g == v)
        return true;
    return false;
  }

  /** @brief Attach @c for_version and the @c AnyX union to a table family.

      One @c Literal overload per SUPPORTED era (mypy narrows each to the
      range's concrete class) plus an @c Expansion → @c AnyX runtime fallback
      gated on @p grid membership, then the importable @c AnyX union — every
      concrete resolved as a runtime attribute lookup, so nothing here
      re-instantiates per family.
      @param tables the @c db.tables submodule owning the classes and the union.
      @param base_name the family's Python name, e.g. @c "Map".
      @param pivots the table's canonicalization pivots (@c detail::{t}_pivots).
      @param grid   the eras the table has a layout for (@c detail::{t}_grid). */
  inline void def_table_facade(nb::module_& tables, std::string_view base_name,
                               std::span<const wowlib::ClientVersion> pivots,
                               std::span<const wowlib::ClientVersion> grid)
  {
    nb::object base = tables.attr(std::string{base_name}.c_str());
    auto eras = std::make_shared<std::vector<FamilyEra>>();
    nb::object any;
    for (const wowlib::Expansion x : expansion_values)
    {
      if (!grid_has(wowlib::to_client_version(x), grid))
        continue;
      nb::object type =
        tables.attr(concrete_name(base_name, x, pivots, grid).c_str());
      def_for_version_overload_erased(base, x, type,
                                      for_version_sig(base_name, x, pivots, grid));
      // Distinct eras can share a canonical range (and thus a class); the
      // union folds duplicates away, the fallback tolerates them.
      any = any.is_valid() ? nb::object(any | type) : type;
      eras->push_back(FamilyEra{x, std::move(type)});
    }
    def_for_version_fallback_erased(base, base_name, std::move(eras));
    tables.attr(("Any" + std::string{base_name}).c_str()) = any;
  }
}
