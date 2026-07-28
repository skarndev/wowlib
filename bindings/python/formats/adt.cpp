/** @file
    @brief Implementation of the ADT versioned-format facade.

    See @c formats/adt.hpp for the surface and @c facade.hpp for the generic
    @c for_version machinery this reuses. The read/write/convert verbs live on
    the welded @c ADTBase (Python only — Lua speaks the templates' own welded
    read/write via @c mark::only(lang::lua)). Like WDT, the ADT assembly pulls
    its physical files through the filesystem gateway, so the
    (FileSystem, FileKey) form is the only complete one. */

#include "formats/adt.hpp"

#include "instantiations/adt.hpp"

#include <format>
#include <string>

#include <wowlib/wowlib.hpp>

#include "facade.hpp"
#include "result_casters.hpp"

namespace wowlib_py::formats::adt
{
  namespace
  {
    /** Every expansion must have an @c adt_versions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::to_client_version(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::adt::adt_versions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs an adt_versions instantiation for its facade");

    /** @brief Run @p fn against @p self cast to its concrete @c ADT<X>, if it is one. */
    template <wowlib::Expansion X, typename Fn>
    bool adt_try(nb::handle self, Fn&& fn)
    {
      using C = wowlib::formats::adt::ADT<wowlib::to_client_version(X)>;
      if (!nb::isinstance<C>(self))
        return false;
      fn(nb::cast<C&>(self));
      return true;
    }

    /** @brief Dispatch @p fn to @p self's concrete @c ADT<X> via isinstance. */
    template <typename Fn>
    void adt_dispatch(nb::handle self, Fn&& fn, const char* what)
    {
      bool done = false;
      template for (constexpr auto e : expansion_enumerators)
        if constexpr (family_has<wowlib::formats::adt::ADT, ([:e:])>)
          if (!done)
            done = adt_try<([:e:])>(self, fn);
      if (!done)
        throw nb::type_error(what);
    }

    /** @brief Convert @p source (any @c ADT<From>) to target expansion @p To. */
    template <wowlib::Expansion To>
    wowlib::Result<typename concrete_of<wowlib::formats::adt::ADT, To>::type>
    convert_adt_from_any(nb::handle source)
    {
      template for (constexpr auto e : expansion_enumerators)
      {
        constexpr wowlib::Expansion From = [:e:];
        using S = wowlib::formats::adt::ADT<wowlib::to_client_version(From)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::has_convert_path<wowlib::formats::adt::ADT,
                                                          wowlib::to_client_version(From),
                                                          wowlib::to_client_version(To)>())
            return wowlib::formats::convert<wowlib::to_client_version(To)>(
              nb::cast<const S&>(source));
          else
            return wowlib::make_error(
              wowlib::ErrorCode::NotImplemented,
              std::format("ADT conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enum_name(From), wowlib::enum_name(To)));
        }
      }
      throw nb::type_error("convert() expects an ADT instance");
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
          return nb::cast(convert_adt_from_any<To>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enum_name(To)} + "]) -> "
                        + concrete_name("ADT", To, wowlib::formats::adt::adt_pivots,
                                        wowlib::formats::adt::adt_versions))));
    }

    /** @brief Attach @c read/@c write/@c convert to @c ADTBase.

        read/write speak the (FileSystem, FileKey, AlphaFormat) triple — the
        assembly locates its physical files through the gateway (the
        "{stem}_tex0.adt" naming convention), and the caller supplies the on-disk
        alpha-map bit depth (from the map's WDT); wowlib does not resolve it.
        @c convert narrows on its target Literal with an @c Expansion → @c AnyADT
        fallback. */
    void def_adt_ops(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key,
           wowlib::formats::adt::AlphaFormat alpha)
        {
          adt_dispatch(
            self,
            [&](auto& tile)
            {
              if (auto r = tile.read(fs, key, alpha); !r)
                throw wowlib::result_error(r.error());
            },
            "expected an ADT instance");
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("key"), nb::arg("alpha"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                "alpha: wowlib.formats.adt.AlphaFormat) -> None"));

      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key,
           wowlib::formats::adt::AlphaFormat alpha)
        {
          adt_dispatch(
            self,
            [&](auto& tile)
            {
              if (auto r = std::as_const(tile).write(fs, key, alpha); !r)
                throw wowlib::result_error(r.error());
            },
            "expected an ADT instance");
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("key"), nb::arg("alpha"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                "alpha: wowlib.formats.adt.AlphaFormat) -> None"));

      // convert(target) — Literal per target (narrows) + Expansion -> AnyADT fallback
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
              result = nb::cast(convert_adt_from_any<To>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyADT"));
    }
  }

  void register_facade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ adt = nb::cast<nb::module_>(formats.attr("adt"));

    namespace fadt = wowlib::formats::adt;
    def_for_version<fadt::ADT>(adt.attr("ADT"), "ADT", fadt::adt_pivots, fadt::adt_versions);
    def_for_version<fadt::MapChunk>(adt.attr("MapChunk"), "MapChunk", fadt::map_chunk_pivots,
                                    fadt::adt_versions);
    def_adt_ops(adt.attr("ADT"));

    def_any_alias<fadt::ADT>(adt, "ADT", fadt::adt_pivots, fadt::adt_versions);
    def_any_alias<fadt::MapChunk>(adt, "MapChunk", fadt::map_chunk_pivots, fadt::adt_versions);
  }
}
