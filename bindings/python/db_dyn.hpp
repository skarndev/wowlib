#pragma once

/** @file
    @brief The generic client-database ergonomics for Python.

    The runtime-schema @c wowlib.db.Table (DynTable) and its Column metadata
    are welded automatically; this unit adds the hand-written surface that
    cannot come from reflection:

    - @c Record — a live row view with ATTRIBUTE access by column name
      (`table[3].map_name`), reading and writing through the generic cell
      accessors with the value shape the column implies (scalar, list for
      arrays, list[str] for locale slots);
    - sequence protocol on the table (`len(table)`, `table[i]`, iteration);
    - @c Table.column(name_or_index) — a ZERO-COPY numpy view of a numeric
      column (rows x elements, exact dtype), or list[str] for string columns;
    - @c wowlib.db.table_names(version=None) — the catalog listing. */

#include <nanobind/nanobind.h>

namespace wowlib_py::db
{
  namespace nb = nanobind;

  /** @brief Attach the generic-table ergonomics to @p module.

      Call from the module body, after welder's walk has registered the
      welded @c db.Table / @c db.Column classes.

      @param module the extension module being initialized. */
  void register_dyn(nb::module_& module);
}
