#pragma once

/** @file
    @brief The FileSystem Python context-manager protocol.

    @c fs.FileSystem exposes explicit lifetime control welded from its protected C++
    surface (@c close/@c is_open). This unit adds the Python-only @c __enter__ /
    @c __exit__ dunders — glue that has no C++ counterpart — so scripts can scope a
    storage handle with a @c with statement. */

#include <nanobind/nanobind.h>

namespace wowlib_py::fs
{
  namespace nb = nanobind;

  /** @brief Attach @c __enter__/@c __exit__ to @c fs.FileSystem.

      @c __enter__ returns self; @c __exit__ forwards to the welded @c close(). Call
      from the module body, after welder's walk has registered @c fs.FileSystem.

      @param module the extension module being initialized. */
  void register_filesystem_protocol(nb::module_& module);
}
