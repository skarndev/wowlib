/** @file
    @brief Implementation of the reflection-generated Python exception hierarchy.

    See @c errors.hpp for the design. Only the limited C API (PyErr_New*,
    PyTuple_Pack) is used, keeping stable-ABI (abi3) builds valid. */

#include "errors.hpp"

#include <array>
#include <string>
#include <utility>

#include <meta>

#include <wowlib/core/error.hpp>

#include "result_casters.hpp"

namespace wowlib_py
{
  namespace
  {
    /** Every @c ErrorCode enumerator, materialized once for the template-for walks. */
    constexpr auto ErrorEnumerators =
      std::define_static_array(std::meta::enumerators_of(^^wowlib::ErrorCode));

    /** The @c wowlib.Error base, and the per-code subclass indexed by enumerator
        value. Owning references held for the module's lifetime. */
    PyObject* gErrorBase = nullptr;
    std::array<PyObject*, ErrorEnumerators.size()> gErrorClasses{};

    /** @brief The Python builtin an @c ErrorCode additionally derives from.

        Lets @c except FileNotFoundError / @c except OSError catch the matching
        wowlib failures. Returns @c nullptr for codes with no natural builtin.
        Extend this switch when a new code has an idiomatic Python counterpart. */
    PyObject* builtinBase(wowlib::ErrorCode code)
    {
      switch (code)
      {
        case wowlib::ErrorCode::FileNotFound: return PyExc_FileNotFoundError;
        case wowlib::ErrorCode::IoError:
        case wowlib::ErrorCode::ListfileIoError: return PyExc_OSError;
        case wowlib::ErrorCode::NotImplemented: return PyExc_NotImplementedError;
        default: return nullptr;
      }
    }

    /** @brief Create @c wowlib.Error and its per-@c ErrorCode subclasses on @p module. */
    void registerErrorHierarchy(nb::module_& module)
    {
      gErrorBase = PyErr_NewExceptionWithDoc(
        "wowlib.Error",
        "Base of every wowlib exception. Instances carry `code` (the ErrorCode "
        "spelling) and `native_error` (the raw StormLib/CascLib/OS error, 0 if "
        "not applicable).",
        PyExc_Exception, nullptr);
      module.attr("Error") = nb::borrow(gErrorBase);

      template for (constexpr auto e : ErrorEnumerators)
      {
        constexpr wowlib::ErrorCode code = [:e:];
        const std::string name{std::meta::identifier_of(e)};
        const std::string qualified = "wowlib." + name;
        const std::string doc = "wowlib failure category " + name + ".";

        PyObject* bases = builtinBase(code)
                            ? PyTuple_Pack(2, gErrorBase, builtinBase(code))
                            : PyTuple_Pack(1, gErrorBase);
        PyObject* cls =
          PyErr_NewExceptionWithDoc(qualified.c_str(), doc.c_str(), bases, nullptr);
        Py_DECREF(bases);
        gErrorClasses[std::to_underlying(code)] = cls;
        module.attr(name.c_str()) = nb::borrow(cls);
      }
    }

    /** @brief Raise the per-code exception for a translated @c ResultError.

        Picks the subclass by @c ErrorCode (falling back to the base), raises it
        with the plain message (no code prefix — the class *is* the code), and
        attaches @c code (the ErrorCode spelling) and @c nativeError (the raw
        backend error). */
    void raiseWowlibError(const wowlib::ResultError& e)
    {
      PyObject* cls = gErrorClasses[std::to_underlying(e.error.code)];
      if (!cls)
        cls = gErrorBase;
      nb::object instance = nb::steal(PyObject_CallFunction(cls, "s", e.error.message.c_str()));
      if (!instance.is_valid())
        return;
      instance.attr("code") = nb::str(std::string{toString(e.error.code)}.c_str());
      instance.attr("native_error") = nb::int_(e.error.nativeError);
      PyErr_SetObject(cls, instance.ptr());
    }
  }

  void registerErrors(nb::module_& module)
  {
    registerErrorHierarchy(module);

    nb::register_exception_translator(
      [](const std::exception_ptr& p, void*)
      {
        try
        {
          std::rethrow_exception(p);
        }
        catch (const wowlib::ResultError& e)
        {
          raiseWowlibError(e);
        }
      });
  }
}
