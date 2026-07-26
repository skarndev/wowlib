#pragma once

/** @file
    @brief The WDL (low-resolution heightmap) versioned-format facade for Python.

    Mirrors @c src/wowlib/formats/wdl/ on the binding side. The per-version
    @c WDL classes and their chunk wire structs are welded automatically; this
    unit adds only the hand-written facade surface that cannot come from
    reflection: @c for_version() and @c convert() on the @c WDL base, and the
    (FileSystem, FileKey) read/write overloads MERGED onto each concrete class
    beside its welded chunk-level read(bytes)/write() pair (attaching them to
    the base would leave them shadowed by the concretes' welded verbs). */

#include <nanobind/nanobind.h>

namespace wowlib_py::formats::wdl
{
  namespace nb = nanobind;

  /** @brief Attach the WDL facade to @p module.

      Wires @c for_version/@c convert onto the @c WDL base and the filesystem
      read/write overloads onto each concrete class. Call from the module
      body, after welder's walk has registered the welded classes.

      @param module the extension module being initialized. */
  void register_facade(nb::module_& module);
}
