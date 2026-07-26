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
    /** Every expansion must have a @c wdl_versions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::to_client_version(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::wdl::wdl_versions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs a wdl_versions instantiation for its facade");

    /** @brief Convert @p source (any @c WDL<From>) to target expansion @p To.
        @throws nanobind::type_error if @p source is not a WDL instance. */
    template <wowlib::Expansion To>
    wowlib::Result<typename concrete_of<wowlib::formats::wdl::WDL, To>::type>
    convert_wdl_from_any(nb::handle source)
    {
      template for (constexpr auto e : expansion_enumerators)
      {
        constexpr wowlib::Expansion From = [:e:];
        using S = wowlib::formats::wdl::WDL<wowlib::to_client_version(From)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::has_convert_path<wowlib::formats::wdl::WDL,
                                                          wowlib::to_client_version(From),
                                                          wowlib::to_client_version(To)>())
            return wowlib::formats::convert<wowlib::to_client_version(To)>(
              nb::cast<const S&>(source));
          else
            return wowlib::make_error(
              wowlib::ErrorCode::NotImplemented,
              std::format("WDL conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enum_name(From), wowlib::enum_name(To)));
        }
      }
      throw nb::type_error("convert() expects a WDL instance");
    }

    /** @brief Attach one @c convert Literal overload (@p To → the concrete class). */
    template <wowlib::Expansion To>
    void def_convert_overload(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::Expansion target) -> nb::object
        {
          if (target != To)
            throw nb::next_overload();
          return nb::cast(convert_wdl_from_any<To>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enum_name(To)} + "]) -> "
                        + concrete_name("WDL", To, wowlib::formats::wdl::wdl_pivots,
                                        wowlib::formats::wdl::wdl_versions))));
    }

    /** @brief Attach @c convert to @c WDLBase (the concretes weld no convert,
        so the base method is inherited unshadowed). */
    void def_wdl_convert(nb::handle base)
    {
      template for (constexpr auto e : expansion_enumerators)
        def_convert_overload<([:e:])>(base);
      nb::cpp_function(
        [](nb::handle self, wowlib::Expansion target)
        {
          nb::object result;
          bool found = false;
          template for (constexpr auto e : expansion_enumerators)
          {
            constexpr wowlib::Expansion To = [:e:];
            if (!found && target == To)
            {
              result = nb::cast(convert_wdl_from_any<To>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyWDL"));
    }

    /** @brief Merge the (FileSystem, FileKey) read/write overloads into ONE
        concrete class's welded verb chain (nanobind merges by name+scope). */
    template <wowlib::Expansion X>
    void def_wdl_fs_verbs_on()
    {
      using C = wowlib::formats::wdl::WDL<wowlib::to_client_version(X)>;
      const nb::handle concrete = nb::type<C>();
      nb::cpp_function(
        [](C& self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          if (auto r = self.read(fs, key); !r)
            throw wowlib::result_error(r.error());
        },
        nb::name("read"), nb::scope(concrete), nb::is_method(),
        nb::arg("source"), nb::arg("key"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));
      nb::cpp_function(
        [](const C& self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          if (auto r = self.write(fs, key); !r)
            throw wowlib::result_error(r.error());
        },
        nb::name("write"), nb::scope(concrete), nb::is_method(),
        nb::arg("dest"), nb::arg("key"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));
    }

    /** @brief Attach the filesystem verbs to every concrete class, once per
        RANGE (several expansions share one concrete). */
    void def_wdl_fs_verbs()
    {
      template for (constexpr auto e : expansion_enumerators)
      {
        constexpr wowlib::Expansion X = [:e:];
        // only the range representative (its own canonical) attaches, so a
        // shared concrete class is not given duplicate overloads
        if constexpr (wowlib::formats::canonical_version(wowlib::to_client_version(X),
                                                         wowlib::formats::wdl::wdl_pivots,
                                                         wowlib::formats::wdl::wdl_versions)
                      == wowlib::to_client_version(X))
          def_wdl_fs_verbs_on<X>();
      }
    }
  }

  void register_facade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ wdl = nb::cast<nb::module_>(formats.attr("wdl"));

    namespace fwdl = wowlib::formats::wdl;
    def_for_version<fwdl::WDL>(wdl.attr("WDL"), "WDL", fwdl::wdl_pivots, fwdl::wdl_versions);
    def_wdl_convert(wdl.attr("WDL"));
    def_wdl_fs_verbs();
    def_any_alias<fwdl::WDL>(wdl, "WDL", fwdl::wdl_pivots, fwdl::wdl_versions);
  }
}
