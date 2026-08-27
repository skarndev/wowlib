#pragma once

/** @file
    @brief The WDT (map description) versioned-format facade for Python.

    Mirrors @c src/wowlib/formats/wdt/ on the binding side. The per-version
    @c WDT, @c WDTRoot and satellite classes and their chunk wire structs are
    welded automatically; this unit adds only the hand-written facade surface
    that cannot come from reflection: @c for_version() on each family base
    (see @c facade.hpp) and the assembly verbs @c read/@c write/@c convert on
    @c WDT. */

#include <nanobind/nanobind.h>

namespace wowlib_py::formats::wdt
{
  namespace nb = nanobind;

  /** @brief Attach the WDT facade to @p module.

      Wires @c for_version onto every WDT family base (WDT, WDTRoot, the
      WDTHeader wire-struct family and the four satellites) and the
      @c read/@c write/@c convert surface onto @c WDT. Call from the module
      body, after welder's walk has registered the welded classes.

      @param module the extension module being initialized. */
  void registerFacade(nb::module_& module);
}
