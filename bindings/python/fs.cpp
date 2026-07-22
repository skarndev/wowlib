/** @file
    @brief Implementation of the FileSystem context-manager protocol. */

#include "fs.hpp"

namespace wowlib_py::fs
{
  void register_filesystem_protocol(nb::module_& module)
  {
    nb::object fs_class = module.attr("fs").attr("FileSystem");
    fs_class.attr("__enter__") =
      nb::cpp_function([](nb::object self) { return self; },
                       nb::name("__enter__"), nb::is_method());
    // __exit__ must take nb::args: nb::handle parameters reject the None that
    // Python passes for (exc_type, exc, tb) on a clean exit.
    fs_class.attr("__exit__") = nb::cpp_function(
      [](nb::object self, nb::args)
      {
        self.attr("close")();
        return false;
      },
      nb::name("__exit__"), nb::is_method());
  }
}
