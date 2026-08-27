#pragma once

/** @file
    @brief The generic client-database ergonomics for Python.

    The runtime-schema @c wowlib.db.Table (DynTable) and its Column metadata
    are welded automatically; this unit adds the hand-written surface that
    cannot come from reflection:

    - @c Record — a live row view with ATTRIBUTE access by column name
      (`table[3].mapName`), reading and writing through the generic cell
      accessors with the value shape the column implies (scalar, list for
      arrays, list[str] for locale slots);
    - sequence protocol on the table (`len(table)`, `table[i]`, iteration);
    - @c Table.column(nameOrIndex) — a ZERO-COPY numpy view of a numeric
      column (rows x elements, exact dtype), or list[str] for string columns;
    - @c wowlib.db.table_names(version=None) — the catalog listing;
    - @c wowlib.db.tables.<era> — one submodule per targeted expansion whose
      attributes are real Table SUBCLASSES created lazily (PEP 562
      __getattr__) from the embedded schema catalog: `tables.wotlk.Map()`
      opens the empty wotlk-era Map. Nothing is compiled per table; the
      dbdgen-generated per-era stubs type the rows era-exactly. */

#include <nanobind/nanobind.h>

namespace wowlib_py::db
{
  namespace nb = nanobind;

  /** @brief Attach the generic-table ergonomics to @p module.

      Call from the module body, after welder's walk has registered the
      welded @c db.Table / @c db.Column classes.

      @param module the extension module being initialized. */
  void registerDyn(nb::module_& module);
}
