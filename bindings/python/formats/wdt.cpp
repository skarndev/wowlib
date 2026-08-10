/** @file
    @brief Implementation of the WDT versioned-format facade.

    See @c formats/wdt.hpp for the surface and @c facade.hpp for the generic
    @c for_version machinery this reuses. The read/write/convert verbs live on
    the welded @c WDTBase (Python only — Lua speaks the templates' own welded
    read/write via @c mark::only(lang::lua)). Like M2, the assembly has no
    buffer-parse overloads: a WDT pulls its satellite files through the
    filesystem gateway, so the (FileSystem, FileKey) form is the only complete
    one — the sub-entities (WDTRoot and the satellites) carry the chunk-level
    read(bytes)/write() pair for buffer work. */

#include "formats/wdt.hpp"

#include "instantiations/wdt.hpp"

#include <format>
#include <string>

#include <wowlib/wowlib.hpp>

#include "facade.hpp"
#include "result_casters.hpp"

namespace wowlib_py::formats::wdt
{
  namespace
  {
    /** Every expansion must have a @c wdt_versions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::to_client_version(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::wdt::wdt_versions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs a wdt_versions instantiation for its facade");

    /** @brief Run @p fn against @p self cast to its concrete @c F<X>, if it is one.
        @return true if @p self was an @c F<X> and @p fn ran. */
    template <template <wowlib::ClientVersion> class F, wowlib::Expansion X, typename Fn>
    bool family_try(nb::handle self, Fn&& fn)
    {
      using C = F<wowlib::to_client_version(X)>;
      if (!nb::isinstance<C>(self))
        return false;
      fn(nb::cast<C&>(self));
      return true;
    }

    /** @brief Dispatch @p fn to @p self's concrete @c F<X> via isinstance.
        @throws nanobind::type_error if @p self is no @c F instance. */
    template <template <wowlib::ClientVersion> class F, typename Fn>
    void family_dispatch(nb::handle self, Fn&& fn, const char* what)
    {
      bool done = false;
      template for (constexpr auto e : expansion_enumerators)
        if constexpr (family_has<F, ([:e:])>)
          if (!done)
            done = family_try<F, ([:e:])>(self, fn);
      if (!done)
        throw nb::type_error(what);
    }

    /** @brief Convert @p source (any @c WDT<From>) to target expansion @p To.

        Identifies the source version by isinstance, then composes the C++
        convert ladder; a pair with no complete @c convert_step ladder degrades
        to a @c NotImplemented Result.
        @throws nanobind::type_error if @p source is not a WDT instance. */
    template <wowlib::Expansion To>
    wowlib::Result<typename concrete_of<wowlib::formats::wdt::WDT, To>::type>
    convert_wdt_from_any(nb::handle source)
    {
      template for (constexpr auto e : expansion_enumerators)
      {
        constexpr wowlib::Expansion From = [:e:];
        using S = wowlib::formats::wdt::WDT<wowlib::to_client_version(From)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::has_convert_path<wowlib::formats::wdt::WDT,
                                                          wowlib::to_client_version(From),
                                                          wowlib::to_client_version(To)>())
            return wowlib::formats::convert<wowlib::to_client_version(To)>(
              nb::cast<const S&>(source));
          else
            return wowlib::make_error(
              wowlib::ErrorCode::NotImplemented,
              std::format("WDT conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enum_name(From), wowlib::enum_name(To)));
        }
      }
      throw nb::type_error("convert() expects a WDT instance");
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
          return nb::cast(convert_wdt_from_any<To>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enum_name(To)} + "]) -> "
                        + concrete_name("WDT", To, wowlib::formats::wdt::wdt_assembly_pivots,
                                        wowlib::formats::wdt::wdt_versions))));
    }

    /** @brief Attach @c read/@c write/@c convert to @c WDTBase.

        read/write speak the (FileSystem, FileKey) pair — the assembly locates
        its satellite files through the gateway (path convention pre-8.1, MPHD
        FileDataIDs after). @c convert narrows on its target Literal with an
        @c Expansion → @c AnyWDT fallback. */
    void def_wdt_ops(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          family_dispatch<wowlib::formats::wdt::WDT>(
            self,
            [&](auto& map)
            {
              if (auto r = map.read(fs, key); !r)
                throw wowlib::result_error(r.error());
            },
            "expected a WDT instance");
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("key"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"),
        "Load the map description — the main file and every satellite present\n"
        "(_occ/_lgt/_fogs/_mpv) — from a client filesystem, replacing this\n"
        "entity's contents. Satellites are located by path convention pre-8.1\n"
        "and by the MPHD FileDataIDs after.\n\n"
        "Args:\n"
        "    source: the filesystem gateway\n"
        "    key: the main .wdt identity (path and/or FileDataID)\n\n"
        "Returns:\n"
        "    nothing; raises on a missing file or malformed chunk stream");

      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          family_dispatch<wowlib::formats::wdt::WDT>(
            self,
            [&](auto& map)
            {
              if (auto r = std::as_const(map).write(fs, key); !r)
                throw wowlib::result_error(r.error());
            },
            "expected a WDT instance");
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("key"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"),
        "Serialize the main file and every engaged satellite through the\n"
        "filesystem's project overlay; satellite names derive from the key.\n\n"
        "Args:\n"
        "    dest: the filesystem gateway\n"
        "    key: the main .wdt identity; must resolve to a path\n\n"
        "Returns:\n"
        "    nothing; raises when the key has no path or a file fails to write");

      // convert(target) — Literal per target (narrows) + Expansion -> AnyWDT fallback
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
              result = nb::cast(convert_wdt_from_any<To>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyWDT"),
        "Rebuild this map description as the target expansion's concrete class,\n"
        "stepping the version ladder one adjacent release at a time (this\n"
        "instance is left unchanged). The return type narrows when the target\n"
        "is a literal.\n\n"
        "Args:\n"
        "    target: the expansion to convert to\n\n"
        "Returns:\n"
        "    the converted map; raises when a ladder step is not implemented");
    }
  }

  void register_facade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ wdt = nb::cast<nb::module_>(formats.attr("wdt"));
    nb::module_ root = nb::cast<nb::module_>(wdt.attr("root"));
    nb::module_ root_chunks = nb::cast<nb::module_>(root.attr("chunks"));
    nb::module_ occlusion = nb::cast<nb::module_>(wdt.attr("occlusion"));
    nb::module_ lights = nb::cast<nb::module_>(wdt.attr("lights"));
    nb::module_ fogs = nb::cast<nb::module_>(wdt.attr("fogs"));
    nb::module_ mpv = nb::cast<nb::module_>(wdt.attr("mpv"));

    // every family hands the facade its canonicalization pivots + grid, so
    // the Literal overload/sig names are the same range-suffixed classes the
    // welded alias tables produce; the satellite families run on their
    // era-subset grids and skip the expansions they exclude
    namespace fwdt = wowlib::formats::wdt;
    def_for_version<fwdt::WDT>(wdt.attr("WDT"), "WDT", fwdt::wdt_assembly_pivots,
                               fwdt::wdt_versions);
    def_for_version<fwdt::root::WDTRoot>(root.attr("WDTRoot"), "WDTRoot", fwdt::wdt_root_pivots,
                                         fwdt::wdt_versions);
    def_for_version<fwdt::root::chunks::SMMapHeader>(root_chunks.attr("WDTHeader"), "WDTHeader",
                                                     fwdt::wdt_header_pivots, fwdt::wdt_versions);
    def_for_version<fwdt::occlusion::WDTOcclusion>(occlusion.attr("WDTOcclusion"), "WDTOcclusion",
                                                   fwdt::wdt_occlusion_pivots,
                                                   fwdt::wdt_satellite_versions);
    def_for_version<fwdt::lights::WDTLights>(lights.attr("WDTLights"), "WDTLights",
                                             fwdt::wdt_lights_pivots,
                                             fwdt::wdt_satellite_versions);
    def_for_version<fwdt::fogs::WDTFogs>(fogs.attr("WDTFogs"), "WDTFogs", fwdt::wdt_fogs_pivots,
                                         fwdt::wdt_fogs_versions);
    def_for_version<fwdt::mpv::WDTParticulates>(mpv.attr("WDTParticulates"), "WDTParticulates",
                                                fwdt::wdt_mpv_pivots, fwdt::wdt_mpv_versions);

    def_wdt_ops(wdt.attr("WDT"));

    // Runtime AnyX union aliases (importable TypeAliases) on the family's own
    // submodule; the satellite families fold only the expansions they exist for.
    def_validation_verbs<fwdt::WDT>(wdt.attr("WDT"), "WDT");
    def_validation_verbs<fwdt::root::WDTRoot>(root.attr("WDTRoot"), "WDTRoot");
    def_validation_verbs<fwdt::occlusion::WDTOcclusion>(occlusion.attr("WDTOcclusion"),
                                                        "WDTOcclusion");
    def_validation_verbs<fwdt::lights::WDTLights>(lights.attr("WDTLights"), "WDTLights");
    def_validation_verbs<fwdt::fogs::WDTFogs>(fogs.attr("WDTFogs"), "WDTFogs");
    def_validation_verbs<fwdt::mpv::WDTParticulates>(mpv.attr("WDTParticulates"),
                                                     "WDTParticulates");

    def_any_alias<fwdt::WDT>(wdt, "WDT", fwdt::wdt_assembly_pivots, fwdt::wdt_versions);
    def_any_alias<fwdt::root::WDTRoot>(root, "WDTRoot", fwdt::wdt_root_pivots,
                                       fwdt::wdt_versions);
    def_any_alias<fwdt::root::chunks::SMMapHeader>(root_chunks, "WDTHeader",
                                                   fwdt::wdt_header_pivots, fwdt::wdt_versions);
    def_any_alias<fwdt::occlusion::WDTOcclusion>(occlusion, "WDTOcclusion",
                                                 fwdt::wdt_occlusion_pivots,
                                                 fwdt::wdt_satellite_versions);
    def_any_alias<fwdt::lights::WDTLights>(lights, "WDTLights", fwdt::wdt_lights_pivots,
                                           fwdt::wdt_satellite_versions);
    def_any_alias<fwdt::fogs::WDTFogs>(fogs, "WDTFogs", fwdt::wdt_fogs_pivots,
                                       fwdt::wdt_fogs_versions);
    def_any_alias<fwdt::mpv::WDTParticulates>(mpv, "WDTParticulates", fwdt::wdt_mpv_pivots,
                                              fwdt::wdt_mpv_versions);
  }
}
