/** @file
    @brief Implementation of the FileSystem context-manager protocol. */

#include "fs.hpp"

namespace wowlib_py::fs
{
  void registerFilesystemProtocol(nb::module_& module)
  {
    nb::object fsClass = module.attr("fs").attr("FileSystem");
    // nb::sig: the lambdas are typed on nb::object, which stubgen would render
    // as an untyped `-> object`; spell the context-manager protocol out so the
    // stubs (and mkdocstrings) show the real types.
    fsClass.attr("__enter__") = nb::cpp_function(
      [](nb::object self) { return self; }, nb::name("__enter__"), nb::is_method(),
      nb::sig("def __enter__(self) -> FileSystem"),
      "Enter a with-block: returns the filesystem itself, unchanged (open() "
      "already opened the storage).");
    // __exit__ must take nb::args: nb::handle parameters reject the None that
    // Python passes for (exc_type, exc, tb) on a clean exit.
    fsClass.attr("__exit__") = nb::cpp_function(
      [](nb::object self, nb::args)
      {
        self.attr("close")();
        return false;
      },
      nb::name("__exit__"), nb::is_method(),
      nb::sig("def __exit__(self, exc_type: object, exc_value: object, "
              "traceback: object, /) -> bool"),
      "Leave the with-block: close()s the filesystem; an in-flight exception "
      "propagates (always returns False).");
  }
}
