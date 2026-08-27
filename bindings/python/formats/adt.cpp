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
    /** Every expansion must have an @c AdtVersions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::toClientVersion(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::adt::AdtVersions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs an adt_versions instantiation for its facade");

    /** @brief Run @p fn against @p self cast to its concrete @c ADT<x>, if it is one. */
    template <wowlib::Expansion X, typename Fn>
    bool adtTry(nb::handle self, Fn&& fn)
    {
      using C = wowlib::formats::adt::ADT<wowlib::toClientVersion(X)>;
      if (!nb::isinstance<C>(self))
        return false;
      fn(nb::cast<C&>(self));
      return true;
    }

    /** @brief Dispatch @p fn to @p self's concrete @c ADT<x> via isinstance. */
    template <typename Fn>
    void adtDispatch(nb::handle self, Fn&& fn, const char* what)
    {
      bool done = false;
      template for (constexpr auto e : ExpansionEnumerators)
        if constexpr (FamilyHas<wowlib::formats::adt::ADT, ([:e:])>)
          if (!done)
            done = adtTry<([:e:])>(self, fn);
      if (!done)
        throw nb::type_error(what);
    }

    /** @brief Convert @p source (any @c ADT<from>) to target expansion @p To. */
    template <wowlib::Expansion To>
    wowlib::Result<typename ConcreteOf<wowlib::formats::adt::ADT, To>::Type>
    convertAdtFromAny(nb::handle source)
    {
      template for (constexpr auto e : ExpansionEnumerators)
      {
        constexpr wowlib::Expansion from = [:e:];
        using S = wowlib::formats::adt::ADT<wowlib::toClientVersion(from)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::hasConvertPath<wowlib::formats::adt::ADT,
                                                          wowlib::toClientVersion(from),
                                                          wowlib::toClientVersion(To)>())
            return wowlib::formats::convert<wowlib::toClientVersion(To)>(
              nb::cast<const S&>(source));
          else
            return wowlib::makeError(
              wowlib::ErrorCode::NotImplemented,
              std::format("ADT conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enumName(from), wowlib::enumName(To)));
        }
      }
      throw nb::type_error("convert() expects an ADT instance");
    }

    /** @brief Attach one @c convert Literal overload (@p To → the concrete class). */
    template <wowlib::Expansion To>
    void defConvertOverload(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::Expansion target) -> nb::object
        {
          if (target != To)
            throw nb::next_overload();
          return nb::cast(convertAdtFromAny<To>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enumName(To)} + "]) -> "
                        + concreteName("ADT", To, wowlib::formats::adt::AdtPivots,
                                        wowlib::formats::adt::AdtVersions))));
    }

    /** @brief Attach @c read/@c write/@c convert to @c ADTBase.

        read/write speak the (FileSystem, FileKey, AlphaFormat) triple — the
        assembly locates its physical files through the gateway (the
        "{stem}_tex0.adt" naming convention), and the caller supplies the on-disk
        alpha-map bit depth (from the map's WDT); wowlib does not resolve it.
        @c convert narrows on its target Literal with an @c Expansion → @c AnyADT
        fallback. */
    void defAdtOps(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key,
           wowlib::formats::adt::AlphaFormat alpha)
        {
          adtDispatch(
            self,
            [&](auto& tile)
            {
              if (auto r = tile.read(fs, key, alpha); !r)
                throw wowlib::ResultError(r.error());
            },
            "expected an ADT instance");
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("key"), nb::arg("alpha"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                "alpha: wowlib.formats.adt.AlphaFormat) -> None"),
        "Load the tile — every split file present — from a client filesystem,\n"
        "replacing this entity's contents. The alpha-map bit depth is supplied by\n"
        "the caller (wowlib does not open the WDT for you).\n\n"
        "Args:\n"
        "    source: the filesystem gateway\n"
        "    key: the tile identity (root .adt path and/or FileDataID)\n"
        "    alpha: the on-disk alpha-map bit depth for this tile's map (from its\n"
        "        WDT MPHD flags)\n\n"
        "Returns:\n"
        "    nothing; raises on a missing tile or malformed chunk stream");

      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key,
           wowlib::formats::adt::AlphaFormat alpha)
        {
          adtDispatch(
            self,
            [&](auto& tile)
            {
              if (auto r = std::as_const(tile).write(fs, key, alpha); !r)
                throw wowlib::ResultError(r.error());
            },
            "expected an ADT instance");
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("key"), nb::arg("alpha"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey, "
                "alpha: wowlib.formats.adt.AlphaFormat) -> None"),
        "Serialize the tile (and, Cata+, every split file) through the\n"
        "filesystem's project overlay; the split-file names derive from the key,\n"
        "which must resolve to a path.\n\n"
        "Args:\n"
        "    dest: the filesystem gateway\n"
        "    key: the tile identity; must resolve to a path\n"
        "    alpha: the on-disk alpha-map bit depth to encode\n\n"
        "Returns:\n"
        "    nothing; raises when the key has no path or a file fails to write");

      // convert(target) — Literal per target (narrows) + Expansion -> AnyADT fallback
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
              result = nb::cast(convertAdtFromAny<to>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyADT"),
        "Rebuild this tile as the target expansion's concrete class, stepping the\n"
        "version ladder one adjacent release at a time (this instance is left\n"
        "unchanged). The return type narrows when the target is a literal.\n\n"
        "Args:\n"
        "    target: the expansion to convert to\n\n"
        "Returns:\n"
        "    the converted tile; raises when a ladder step is not implemented");
    }
  }

  void registerFacade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ adt = nb::cast<nb::module_>(formats.attr("adt"));

    namespace fadt = wowlib::formats::adt;
    defForVersion<fadt::ADT>(adt.attr("ADT"), "ADT", fadt::AdtPivots, fadt::AdtVersions);
    defForVersion<fadt::MapChunk>(adt.attr("MapChunk"), "MapChunk", fadt::MapChunkPivots,
                                    fadt::AdtVersions);
    defAdtOps(adt.attr("ADT"));

    defValidationVerbs<fadt::ADT>(adt.attr("ADT"), "ADT");
    defValidationVerbs<fadt::MapChunk>(adt.attr("MapChunk"), "MapChunk");

    defAnyAlias<fadt::ADT>(adt, "ADT", fadt::AdtPivots, fadt::AdtVersions);
    defAnyAlias<fadt::MapChunk>(adt, "MapChunk", fadt::MapChunkPivots, fadt::AdtVersions);
  }
}
