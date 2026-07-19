// The Python extension: welder reflects namespace wowlib and lays the nanobind
// bindings down. Callables and data reshape to PEP 8 snake_case, but class,
// enum and enumerator identifiers bind VERBATIM (wowlib_python_naming below):
// wowlib's type names are the client's canonical spellings (SMOHeader, WMORoot,
// FileDataID) and acronym normalization would corrupt them (Smo, Wmo,
// FileDataId). Docstrings render Google-style; annotations come straight from
// the wowlib headers.
//
// Result<T> returns are unwrapped by the casters in result_casters.hpp: the
// success branch converts as T, the error branch throws wowlib::result_error,
// which the translator below maps onto a reflection-generated exception
// hierarchy: wowlib.Error at the root and one class per ErrorCode enumerator
// (wowlib.FileNotFound, wowlib.StorageOpenFailed, ...) — the class identity IS
// the error code, and new codes grow new classes with no binding changes.
#include <array>
#include <format>
#include <string>
#include <utility>

#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <wowlib/wowlib.hpp>

#include "result_casters.hpp"

#include <welder/naming.hpp>
#include <welder/rods/python/nanobind/module.hpp>

namespace
{
  /** wowlib's Python naming: PEP 8 snake_case for callables, data members and
      submodules, but class, enum and enumerator identifiers bind verbatim —
      wowlib's C++ type names are already the client's canonical spellings
      (SMOHeader, WMORoot, FileDataID), and CapWords acronym normalization
      would corrupt them (Smo, Wmo, FileDataId). Satisfies
      welder::naming::name_style. */
  struct wowlib_python_naming : welder::naming::snake_case
  {
    /** Classes bind under their C++ identifier, unchanged. */
    static consteval std::string transform_class(std::meta::info e)
    {
      return std::string{std::meta::identifier_of(e)};
    }

    /** Enum types bind under their C++ identifier, unchanged. */
    static consteval std::string transform_enum(std::meta::info e)
    {
      return std::string{std::meta::identifier_of(e)};
    }

    /** Enumerators bind under their C++ identifier, unchanged. */
    static consteval std::string transform_enumerator(std::meta::info e)
    {
      return std::string{std::meta::identifier_of(e)};
    }
  };
  static_assert(welder::naming::name_style<wowlib_python_naming>);

  constexpr auto error_enumerators =
    std::define_static_array(std::meta::enumerators_of(^^wowlib::ErrorCode));

  // The generated exception classes, indexed by the enumerator's value; owned
  // references held for the module's lifetime.
  PyObject* error_base = nullptr;
  std::array<PyObject*, error_enumerators.size()> error_classes{};

  /** The Python builtin an ErrorCode's exception additionally derives from, so
      idiomatic handlers (`except FileNotFoundError:`) catch wowlib failures too;
      nullptr when only wowlib.Error applies. */
  PyObject* builtin_base(wowlib::ErrorCode code)
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

  /** Build wowlib.Error and the per-code subclasses and publish them as module
      attributes. */
  void register_error_hierarchy(nanobind::module_& module)
  {
    error_base = PyErr_NewExceptionWithDoc(
      "wowlib.Error",
      "Base of every wowlib exception. Instances carry `code` (the ErrorCode "
      "spelling, e.g. 'FileNotFound') and `native_error` (the raw "
      "StormLib/CascLib/OS error value, 0 if not applicable).",
      PyExc_Exception, nullptr);
    module.attr("Error") = nanobind::borrow(error_base);

    template for (constexpr auto e : error_enumerators)
    {
      constexpr wowlib::ErrorCode code = [:e:];
      const std::string name{std::meta::identifier_of(e)};
      const std::string qualified = "wowlib." + name;
      const std::string doc = "wowlib failure category " + name + ".";

      PyObject* bases = builtin_base(code)
                          ? PyTuple_Pack(2, error_base, builtin_base(code))
                          : PyTuple_Pack(1, error_base);
      PyObject* cls =
        PyErr_NewExceptionWithDoc(qualified.c_str(), doc.c_str(), bases, nullptr);
      Py_DECREF(bases);

      error_classes[std::to_underlying(code)] = cls;
      module.attr(name.c_str()) = nanobind::borrow(cls);
    }
  }

  constexpr auto expansion_enumerators =
    std::define_static_array(std::meta::enumerators_of(^^wowlib::Expansion));

  /** The enumerator identifier of @a expansion ("Wotlk", ...). */
  consteval std::string_view expansion_name(wowlib::Expansion expansion)
  {
    for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
      if (std::meta::extract<wowlib::Expansion>(e) == expansion)
        return std::meta::identifier_of(e);
    return {};
  }

  /** The welded per-version class name ("WMOWotlk"), as a static string. */
  consteval std::string wmo_class_name(wowlib::Expansion expansion)
  {
    return "WMO" + std::string{expansion_name(expansion)};
  }

  /** The typed load_wmo overload signature for @a expansion. */
  consteval const char* load_wmo_sig(wowlib::Expansion expansion)
  {
    std::string sig = "def load_wmo(fs: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                      "expansion: Literal[wowlib.Expansion.";
    sig += expansion_name(expansion);
    sig += "]) -> ";
    sig += wmo_class_name(expansion);
    return std::define_static_string(sig);
  }

  /** The typed convert_wmo overload signature for @a expansion. */
  consteval const char* convert_wmo_sig(wowlib::Expansion expansion)
  {
    std::string sig = "def convert_wmo(wmo: ";
    sig += wmo_class_name(expansion);
    sig += ", target: Literal[wowlib.Expansion.";
    sig += expansion_name(expansion);
    sig += "]) -> ";
    sig += wmo_class_name(expansion);
    return std::define_static_string(sig);
  }

  /** "WMOVanilla | WMOTbc | ..." — the union of every per-version class, for
      the convert_wmo catch-all's signature. */
  consteval std::string wmo_class_union()
  {
    std::string all;
    for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
    {
      if (!all.empty())
        all += " | ";
      all += wmo_class_name(std::meta::extract<wowlib::Expansion>(e));
    }
    return all;
  }

  /** The convert_wmo cross-version catch-all signature. */
  consteval const char* convert_wmo_catchall_sig()
  {
    std::string sig = "def convert_wmo(wmo: " + wmo_class_union()
                      + ", target: wowlib.Expansion) -> " + wmo_class_union();
    return std::define_static_string(sig);
  }

  /** Register the load_wmo/convert_wmo overload pair for one expansion. A
      function template (not the `template for` body itself) because gcc 16
      fails to complete the WMO instantiation from inside lambdas nested in an
      expanded loop body. The docstring rides on the first overload only.
      @tparam X the expansion this overload dispatches on. */
  template <wowlib::Expansion X>
  void def_wmo_factories(nanobind::module_& formats)
  {
    using W = wowlib::formats::wmo::WMO<wowlib::to_client_version(X)>;

    const auto expansion_is = [](wowlib::Expansion have)
    {
      if (have != X)
        throw nanobind::next_overload();
    };

    constexpr bool first = X == wowlib::Expansion::Vanilla;
    const auto load = [expansion_is](wowlib::fs::FileSystem& fs, const wowlib::FileKey& key,
                                     wowlib::Expansion have)
    {
      expansion_is(have);
      return W::load(fs, key);
    };
    // conversion: identity works today, cross-version steps are a later
    // milestone — the machinery and its typed stubs are already in place
    const auto convert = [expansion_is](const W& entity, wowlib::Expansion target)
    {
      expansion_is(target);
      return wowlib::formats::convert<wowlib::to_client_version(X)>(entity);
    };

    if constexpr (first)
    {
      formats.def("load_wmo", load, nanobind::sig(load_wmo_sig(X)),
                  "Load a WMO (root plus groups) for the given expansion.");
      formats.def("convert_wmo", convert, nanobind::sig(convert_wmo_sig(X)),
                  "Convert a WMO to another expansion's representation.");
    }
    else
    {
      formats.def("load_wmo", load, nanobind::sig(load_wmo_sig(X)));
      formats.def("convert_wmo", convert, nanobind::sig(convert_wmo_sig(X)));
    }
  }

  /** The versioned-format factories: one Python name, one overload per
      Expansion enumerator, generated with `template for` so a new expansion in
      the enum grows its overload automatically (the static_assert holds the
      instantiation list to the same coverage). nanobind overloads cannot
      dispatch on an enum's VALUE, so each overload rejects foreign expansions
      with next_overload(); each carries its own `nb::sig` naming the Literal
      expansion and the exact return class, which stubgen renders as typed
      @overload blocks — mypy then narrows load_wmo(fs, key, Expansion.Wotlk)
      to WMOWotlk. */
  void register_format_factories(nanobind::module_& module)
  {
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::to_client_version(std::meta::extract<wowlib::Expansion>(e));
          bool instantiated = false;
          for (const auto& supported : wowlib::formats::wmo::wmo_versions)
            instantiated = instantiated || supported == version;
          if (!instantiated)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs a WMO instantiation (wmo_versions) for its factory");

    auto formats = nanobind::cast<nanobind::module_>(module.attr("formats"));

    template for (constexpr auto e : expansion_enumerators)
      def_wmo_factories<([:e:])>(formats);

    formats.def(
      "convert_wmo",
      [](nanobind::object, wowlib::Expansion target)
        -> wowlib::Result<wowlib::formats::WMOWotlk>
      {
        return wowlib::make_error(
          wowlib::ErrorCode::NotImplemented,
          std::format("cross-version WMO conversion (to {}) has no convert steps yet",
                      wowlib::enum_name(target)));
      },
      nanobind::sig(convert_wmo_catchall_sig()));
  }

  /** Map a result_error onto its generated class: instantiate with the plain
      message (the class name already spells the category), attach the
      machine-readable attributes, raise. */
  void raise_wowlib_error(const wowlib::result_error& e)
  {
    PyObject* cls = error_classes[std::to_underlying(e.error.code)];
    if (!cls)
      cls = error_base;

    nanobind::object instance = nanobind::steal(
      PyObject_CallFunction(cls, "s", e.error.message.c_str()));
    if (!instance.is_valid())
      return;  // the constructor's own error is already set

    instance.attr("code") = nanobind::str(std::string{to_string(e.error.code)}.c_str());
    instance.attr("native_error") = nanobind::int_(e.error.native_error);
    PyErr_SetObject(cls, instance.ptr());
  }
}

WELDER_MODULE(wowlib, nanobind,
              welder::welder<welder::rods::nanobind::rod<>, wowlib_python_naming>)
{
  register_error_hierarchy(module);
  register_format_factories(module);

  // Context-manager protocol on the facade: acquisition is FileSystem.open(),
  // the with-block scopes the release. Attached here (not in C++) because the
  // dunder slots are a Python protocol, not part of the C++ surface — __exit__
  // simply forwards to the welded close().
  nanobind::object fs_class = module.attr("fs").attr("FileSystem");
  fs_class.attr("__enter__") = nanobind::cpp_function(
    [](nanobind::object self) { return self; },
    nanobind::name("__enter__"), nanobind::is_method());
  fs_class.attr("__exit__") = nanobind::cpp_function(
    [](nanobind::object self, nanobind::args)  // (exc_type, exc_val, exc_tb) unused
    {
      self.attr("close")();
      return false;  // never suppress the in-flight exception
    },
    nanobind::name("__exit__"), nanobind::is_method());

  nanobind::register_exception_translator(
    [](const std::exception_ptr& p, void*)
    {
      try
      {
        std::rethrow_exception(p);
      }
      catch (const wowlib::result_error& e)
      {
        raise_wowlib_error(e);
      }
      // anything else propagates to the next translator
    });
}