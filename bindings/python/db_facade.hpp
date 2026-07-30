#pragma once

/** @file
    @brief The version-keyed facade for the generated client-database tables.

    Every table is a class template @c F<ClientVersion> (e.g. @c db::tables::Map)
    with a welded empty base (@c Map_, welded as @c Map) that each range
    instantiation inherits — so welder gives Python real inheritance and
    @c isinstance for free. This header adds the version-keyed constructor
    @c for_version() and the @c AnyX union, exactly like the format facade
    (facade.hpp), whose helpers it reuses.

    The one difference from the format families: a table alias @c F<V> is TOTAL
    (it canonicalizes every version into its grid), so the format facade's
    @c family_has SFINAE gate — which excludes eras a constrained template refuses
    to instantiate — never fires here. A table instead carries a GRID of the eras
    it actually has a layout for, and the facade gates @c for_version on grid
    membership: @c Map.for_version(Expansion.Cata) on a table with no Cata block
    raises, rather than silently collapsing Cata onto the wotlk range. */

#include <span>
#include <string>

#include <nanobind/nanobind.h>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/expansion.hpp>

#include "facade.hpp" // make_one, concrete_name, persist, def_any_alias, ...

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

  /** The @c for_version runtime fallback: dispatch @p expansion to its concrete
      table, but only for the eras @p grid supports (others raise). */
  template <template <wowlib::ClientVersion> class F>
  nb::object make_for_supported(wowlib::Expansion expansion,
                                std::span<const wowlib::ClientVersion> grid)
  {
    nb::object result;
    bool found = false;
    template for (constexpr auto e : expansion_enumerators)
    {
      constexpr wowlib::Expansion X = [:e:];
      if (!found && expansion == X
          && grid_has(wowlib::to_client_version(X), grid))
      {
        result = make_one<F, X>();
        found = true;
      }
    }
    if (!found)
      throw nb::value_error("this table has no layout for that expansion");
    return result;
  }

  /** @brief Attach @c for_version and the @c AnyX union to table family @p F.

      One @c Literal overload per SUPPORTED era (mypy narrows each to the range's
      concrete class) plus an @c Expansion → @c AnyX runtime fallback, then the
      importable @c AnyX union — all reusing the format-facade helpers, only the
      overload set gated on @p grid membership instead of @c family_has.

      @tparam F the table alias template (e.g. @c db::tables::Map).
      @param tables the @c db.tables submodule owning the classes and the union.
      @param base_name the base's Python name, e.g. @c "Map".
      @param pivots the table's canonicalization pivots (@c detail::{t}_pivots).
      @param grid   the eras the table has a layout for (@c detail::{t}_grid). */
  template <template <wowlib::ClientVersion> class F>
  void def_table_facade(nb::module_& tables, std::string_view base_name,
                        std::span<const wowlib::ClientVersion> pivots,
                        std::span<const wowlib::ClientVersion> grid)
  {
    nb::object base = tables.attr(std::string{base_name}.c_str());
    template for (constexpr auto e : expansion_enumerators)
      if (grid_has(wowlib::to_client_version([:e:]), grid))
        def_for_version_overload<F, ([:e:])>(base, base_name, pivots, grid);
    nb::cpp_function(
      [grid](wowlib::Expansion expansion)
      { return make_for_supported<F>(expansion, grid); },
      nb::name("for_version"), nb::scope(base), nb::arg("expansion"),
      nb::sig(persist("def for_version(expansion: wowlib.Expansion) -> Any"
                      + std::string{base_name})));
    def_any_alias<F>(tables, base_name, pivots, grid);
  }
}
