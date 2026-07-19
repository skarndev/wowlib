// The Python extension: welder reflects namespace wowlib and lays the nanobind
// bindings down. Names are reshaped to PEP 8 (read_file stays read_file, factory
// spellings like `wotlk` are already pythonic); docstrings render Google-style;
// annotations come straight from the wowlib headers.
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

#include <welder/rods/python/nanobind/module.hpp>
#include <welder/rods/python/naming.hpp>

namespace
{
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

  /** The versioned-format factories: one Python name, one overload per
      instantiated version. nanobind overloads cannot dispatch on an enum's
      VALUE, so each overload rejects foreign expansions with next_overload();
      each carries its own `nb::sig` naming the Literal expansion and the exact
      return class, which stubgen renders as typed @overload blocks — mypy then
      narrows load_wmo(fs, key, Expansion.Wotlk) to WmoWotlk. The trailing
      catch-all raises UnsupportedClientVersion for versions with no
      instantiation. Keep the overload set in sync with wmo_versions. */
  void register_format_factories(nanobind::module_& module)
  {
    static_assert(wowlib::formats::wmo::wmo_versions.size() == 2,
                  "a new Wmo version needs a factory overload here");

    auto formats = nanobind::cast<nanobind::module_>(module.attr("formats"));

    const auto expansion_is = [](wowlib::Expansion have, wowlib::Expansion want)
    {
      if (have != want)
        throw nanobind::next_overload();
    };

    formats.def(
      "load_wmo",
      [expansion_is](wowlib::fs::FileSystem& fs, const wowlib::FileKey& key,
                     wowlib::Expansion expansion)
      {
        expansion_is(expansion, wowlib::Expansion::Wotlk);
        return wowlib::formats::WmoWotlk::load(fs, key);
      },
      nanobind::sig("def load_wmo(fs: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                    "expansion: Literal[wowlib.Expansion.Wotlk]) -> WmoWotlk"),
      "Load a WMO (root plus groups) for the given expansion.");
    formats.def(
      "load_wmo",
      [expansion_is](wowlib::fs::FileSystem& fs, const wowlib::FileKey& key,
                     wowlib::Expansion expansion)
      {
        expansion_is(expansion, wowlib::Expansion::Shadowlands);
        return wowlib::formats::WmoShadowlands::load(fs, key);
      },
      nanobind::sig("def load_wmo(fs: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                    "expansion: Literal[wowlib.Expansion.Shadowlands]) -> WmoShadowlands"));
    formats.def(
      "load_wmo",
      [](wowlib::fs::FileSystem&, const wowlib::FileKey&, wowlib::Expansion expansion)
        -> wowlib::Result<wowlib::formats::WmoWotlk>
      {
        return wowlib::make_error(
          wowlib::ErrorCode::UnsupportedClientVersion,
          std::format("no Wmo instantiation for expansion {}",
                      wowlib::enum_name(expansion)));
      },
      nanobind::sig("def load_wmo(fs: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                    "expansion: wowlib.Expansion) -> WmoWotlk | WmoShadowlands"));

    // conversion: identity works today, cross-version steps are a later
    // milestone — the machinery and its typed stubs are already in place
    formats.def(
      "convert_wmo",
      [expansion_is](const wowlib::formats::WmoWotlk& wmo, wowlib::Expansion target)
      {
        expansion_is(target, wowlib::Expansion::Wotlk);
        return wowlib::formats::convert<wowlib::versions::wotlk>(wmo);
      },
      nanobind::sig("def convert_wmo(wmo: WmoWotlk, "
                    "target: Literal[wowlib.Expansion.Wotlk]) -> WmoWotlk"),
      "Convert a WMO to another expansion's representation.");
    formats.def(
      "convert_wmo",
      [expansion_is](const wowlib::formats::WmoShadowlands& wmo, wowlib::Expansion target)
      {
        expansion_is(target, wowlib::Expansion::Shadowlands);
        return wowlib::formats::convert<wowlib::versions::shadowlands>(wmo);
      },
      nanobind::sig("def convert_wmo(wmo: WmoShadowlands, "
                    "target: Literal[wowlib.Expansion.Shadowlands]) -> WmoShadowlands"));
    formats.def(
      "convert_wmo",
      [](nanobind::object, wowlib::Expansion target) -> wowlib::Result<wowlib::formats::WmoWotlk>
      {
        return wowlib::make_error(
          wowlib::ErrorCode::NotImplemented,
          std::format("cross-version WMO conversion (to {}) has no convert steps yet",
                      wowlib::enum_name(target)));
      },
      nanobind::sig("def convert_wmo(wmo: WmoWotlk | WmoShadowlands, "
                    "target: wowlib.Expansion) -> WmoWotlk | WmoShadowlands"));
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
              welder::welder<welder::rods::nanobind::rod<>,
                             welder::rods::python::pep8>)
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