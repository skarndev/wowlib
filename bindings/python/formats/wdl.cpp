/** @file
    @brief Implementation of the WDL versioned-format facade.

    See @c formats/wdl.hpp for the surface and @c facade.hpp for the generic
    @c for_version machinery this reuses. Unlike the assemblies (WMO/M2/WDT),
    the WDL entity IS a chunked file, so its concretes already weld the
    chunk-level read(bytes)/write() pair — the (FileSystem, FileKey) overloads
    are merged INTO each concrete's overload chain (nanobind merges
    cpp_functions by name+scope), because a base-scoped verb would be shadowed
    by the concrete's welded one in Python's attribute lookup. */

#include "formats/wdl.hpp"

#include "instantiations/wdl.hpp"

#include <format>
#include <string>

#include <wowlib/wowlib.hpp>

#include "facade.hpp"
#include "result_casters.hpp"

namespace wowlib_py::formats::wdl
{
  namespace
  {
    /** Every expansion must have a @c WdlVersions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::toClientVersion(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::wdl::WdlVersions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs a wdl_versions instantiation for its facade");

    /** @brief Convert @p source (any @c WDL<from>) to target expansion @p to.
        @throws nanobind::type_error if @p source is not a WDL instance. */
    template <wowlib::Expansion to>
    wowlib::Result<typename ConcreteOf<wowlib::formats::wdl::WDL, to>::Type>
    convertWdlFromAny(nb::handle source)
    {
      template for (constexpr auto e : ExpansionEnumerators)
      {
        constexpr wowlib::Expansion from = [:e:];
        using S = wowlib::formats::wdl::WDL<wowlib::toClientVersion(from)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::hasConvertPath<wowlib::formats::wdl::WDL,
                                                          wowlib::toClientVersion(from),
                                                          wowlib::toClientVersion(to)>())
            return wowlib::formats::convert<wowlib::toClientVersion(to)>(
              nb::cast<const S&>(source));
          else
            return wowlib::makeError(
              wowlib::ErrorCode::NotImplemented,
              std::format("WDL conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enumName(from), wowlib::enumName(to)));
        }
      }
      throw nb::type_error("convert() expects a WDL instance");
    }

    /** @brief Attach one @c convert Literal overload (@p to → the concrete class). */
    template <wowlib::Expansion to>
    void defConvertOverload(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::Expansion target) -> nb::object
        {
          if (target != to)
            throw nb::next_overload();
          return nb::cast(convertWdlFromAny<to>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enumName(to)} + "]) -> "
                        + concreteName("WDL", to, wowlib::formats::wdl::WdlPivots,
                                        wowlib::formats::wdl::WdlVersions))));
    }

    /** @brief Attach @c convert to @c WDLBase (the concretes weld no convert,
        so the base method is inherited unshadowed). */
    void defWdlConvert(nb::handle base)
    {
      template for (constexpr auto e : ExpansionEnumerators)
        defConvertOverload<([:e:])>(base);
      nb::cpp_function(
        [](nb::handle self, wowlib::Expansion target)
        {
          nb::object result;
          bool found = false;
          template for (constexpr auto e : ExpansionEnumerators)
          {
            constexpr wowlib::Expansion to = [:e:];
            if (!found && target == to)
            {
              result = nb::cast(convertWdlFromAny<to>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyWDL"),
        "Rebuild this heightmap as the target expansion's concrete class,\n"
        "stepping the version ladder one adjacent release at a time (this\n"
        "instance is left unchanged). The return type narrows when the target\n"
        "is a literal.\n\n"
        "Args:\n"
        "    target: the expansion to convert to\n\n"
        "Returns:\n"
        "    the converted heightmap; raises when a ladder step is not implemented");
    }

    /** @brief Merge the (FileSystem, FileKey) read/write overloads into ONE
        concrete class's welded verb chain (nanobind merges by name+scope). */
    template <wowlib::Expansion x>
    void defWdlFsVerbsOn()
    {
      using C = wowlib::formats::wdl::WDL<wowlib::toClientVersion(x)>;
      const nb::handle concrete = nb::type<C>();
      nb::cpp_function(
        [](C& self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          if (auto r = self.read(fs, key); !r)
            throw wowlib::ResultError(r.error());
        },
        nb::name("read"), nb::scope(concrete), nb::is_method(),
        nb::arg("source"), nb::arg("key"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"),
        "Load the .wdl from a client filesystem, replacing this entity's\n"
        "contents.\n\n"
        "Args:\n"
        "    source: the filesystem gateway\n"
        "    key: the .wdl identity (path and/or FileDataID)\n\n"
        "Returns:\n"
        "    nothing; raises on a missing file or malformed chunk stream");
      nb::cpp_function(
        [](const C& self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          if (auto r = self.write(fs, key); !r)
            throw wowlib::ResultError(r.error());
        },
        nb::name("write"), nb::scope(concrete), nb::is_method(),
        nb::arg("dest"), nb::arg("key"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"),
        "Serialize the .wdl through the filesystem's project overlay.\n\n"
        "Args:\n"
        "    dest: the filesystem gateway\n"
        "    key: the .wdl identity; must resolve to a path\n\n"
        "Returns:\n"
        "    nothing; raises when the key has no path or the file fails to write");
    }

    /** @brief Attach the filesystem verbs to every concrete class, once per
        RANGE (several expansions share one concrete). */
    void defWdlFsVerbs()
    {
      template for (constexpr auto e : ExpansionEnumerators)
      {
        constexpr wowlib::Expansion x = [:e:];
        // only the range representative (its own canonical) attaches, so a
        // shared concrete class is not given duplicate overloads
        if constexpr (wowlib::formats::canonicalVersion(wowlib::toClientVersion(x),
                                                         wowlib::formats::wdl::WdlPivots,
                                                         wowlib::formats::wdl::WdlVersions)
                      == wowlib::toClientVersion(x))
          defWdlFsVerbsOn<x>();
      }
    }
  }

  void registerFacade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ wdl = nb::cast<nb::module_>(formats.attr("wdl"));

    namespace fwdl = wowlib::formats::wdl;
    defForVersion<fwdl::WDL>(wdl.attr("WDL"), "WDL", fwdl::WdlPivots, fwdl::WdlVersions);
    defWdlConvert(wdl.attr("WDL"));
    defWdlFsVerbs();
    defValidationVerbs<fwdl::WDL>(wdl.attr("WDL"), "WDL");

    defAnyAlias<fwdl::WDL>(wdl, "WDL", fwdl::WdlPivots, fwdl::WdlVersions);
  }
}
