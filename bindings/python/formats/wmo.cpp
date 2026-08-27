/** @file
    @brief Implementation of the WMO versioned-format facade.

    See @c formats/wmo.hpp for the surface and @c facade.hpp for the generic
    @c for_version machinery this reuses. The read/write/convert verbs live on the
    welded @c WMOBase (Python only — Lua speaks the templates' own welded read/write
    via @c mark::only(lang::lua)), so per-expansion @c nb::sig overloads render as
    typed @c \@overload blocks and the concrete classes inherit them unchanged. */

#include "formats/wmo.hpp"

#include "instantiations/wmo.hpp"

#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <wowlib/wowlib.hpp>

#include "buffers.hpp"
#include "facade.hpp"
#include "result_casters.hpp"

namespace wowlib_py::formats::wmo
{
  namespace
  {
    /** Every expansion must have a @c WmoVersions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::toClientVersion(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::wmo::WmoVersions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs a wmo_versions instantiation for its facade");

    /** @brief Run @p fn against @p self cast to its concrete @c WMO<x>, if it is one.
        @return true if @p self was a @c WMO<x> and @p fn ran. */
    template <wowlib::Expansion x, typename Fn>
    bool wmoTry(nb::handle self, Fn&& fn)
    {
      using W = wowlib::formats::wmo::WMO<wowlib::toClientVersion(x)>;
      if (!nb::isinstance<W>(self))
        return false;
      fn(nb::cast<W&>(self));
      return true;
    }

    /** @brief Dispatch @p fn to @p self's concrete @c WMO<x> via isinstance.
        @throws nanobind::type_error if @p self is not a WMO instance. */
    template <typename Fn>
    void wmoDispatch(nb::handle self, Fn&& fn)
    {
      bool done = false;
      template for (constexpr auto e : ExpansionEnumerators)
        if (!done)
          done = wmoTry<([:e:])>(self, fn);
      if (!done)
        throw nb::type_error("expected a WMO instance");
    }

    /** @brief Convert @p source (any @c WMO<from>) to target expansion @p to.

        Identifies the source version by isinstance, then composes the C++ convert
        ladder. A pair with no complete @c convert_step ladder degrades to a
        @c NotImplemented Result (identity always works; writing the C++ steps lights
        the pair up here with no glue change).
        @throws nanobind::type_error if @p source is not a WMO instance. */
    template <wowlib::Expansion to>
    wowlib::Result<typename ConcreteOf<wowlib::formats::wmo::WMO, to>::Type>
    convertWmoFromAny(nb::handle source)
    {
      template for (constexpr auto e : ExpansionEnumerators)
      {
        constexpr wowlib::Expansion from = [:e:];
        using S = wowlib::formats::wmo::WMO<wowlib::toClientVersion(from)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::hasConvertPath<wowlib::formats::wmo::WMO,
                                                          wowlib::toClientVersion(from),
                                                          wowlib::toClientVersion(to)>())
            return wowlib::formats::convert<wowlib::toClientVersion(to)>(nb::cast<const S&>(source));
          else
            return wowlib::makeError(
              wowlib::ErrorCode::NotImplemented,
              std::format("WMO conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enumName(from), wowlib::enumName(to)));
        }
      }
      throw nb::type_error("convert() expects a WMO instance");
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
          return nb::cast(convertWmoFromAny<to>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enumName(to)} + "]) -> "
                        + concreteName("WMO", to, wowlib::formats::wmo::WmoAssemblyPivots,
                                        wowlib::formats::wmo::WmoVersions))));
    }

    /** @brief Attach @c read/@c write/@c convert to @c WMOBase.

        Each verb is a small set of native overloads (so stubgen renders typed
        @c \@overload blocks). @c read/@c write speak either a @c (FileSystem,
        FileKey) pair or in-memory buffers / binary file-likes; @c convert narrows
        on its target Literal with an @c Expansion → @c AnyWMO fallback. */
    void defWmoOps(nb::handle base)
    {
      // read(FileSystem, FileKey) — load in place
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          wmoDispatch(self, [&](auto& wmo)
          {
            if (auto r = wmo.read(fs, key); !r)
              throw wowlib::ResultError(r.error());
          });
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("key"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"),
        "Load the assembly — the root file and every numbered group file — from\n"
        "a client filesystem, replacing this entity's contents.\n\n"
        "Args:\n"
        "    source: the filesystem gateway\n"
        "    key: the root file identity; the \"_000\" … group keys derive from it\n\n"
        "Returns:\n"
        "    nothing; raises on a missing file or malformed chunk stream");
      // read(rootBuffer, groupBuffers) — parse in place from bytes / file-likes
      nb::cpp_function(
        [](nb::handle self, nb::object root, nb::object groups)
        {
          const wowlib::FileBuffer rootBuffer = toBuffer(root);
          std::vector<wowlib::FileBuffer> groupBuffers;
          const std::size_t n = nb::len(groups);
          groupBuffers.reserve(n);
          for (std::size_t i = 0; i < n; ++i)
            groupBuffers.push_back(toBuffer(nb::object(groups[i])));
          std::vector<std::span<const std::byte>> spans;
          spans.reserve(groupBuffers.size());
          for (const auto& g : groupBuffers)
            spans.emplace_back(g);
          wmoDispatch(self, [&](auto& wmo)
          {
            if (auto r = wmo.read(std::span<const std::byte>{rootBuffer}, std::span{spans}); !r)
              throw wowlib::ResultError(r.error());
          });
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("groups"),
        nb::sig("def read(self, source: collections.abc.Buffer | typing.BinaryIO, "
                "groups: collections.abc.Sequence[collections.abc.Buffer | typing.BinaryIO]) -> None"),
        "Parse the assembly from memory: the root image plus one buffer (or\n"
        "binary file-like) per group file, in group order.\n\n"
        "Args:\n"
        "    source (Buffer | BinaryIO): the root file bytes\n"
        "    groups (Sequence[Buffer | BinaryIO]): every group file's bytes, ordered\n\n"
        "Returns:\n"
        "    nothing; raises on a malformed chunk stream");

      // write(FileSystem, FileKey) — save; group names inferred from the root key
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          wmoDispatch(self, [&](auto& wmo)
          {
            if (auto r = std::as_const(wmo).write(fs, key); !r)
              throw wowlib::ResultError(r.error());
          });
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("key"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"),
        "Serialize the root and every group file through the filesystem's\n"
        "project overlay.\n\n"
        "Args:\n"
        "    dest: the filesystem gateway\n"
        "    key: the root file identity; must resolve to a path, from which the\n"
        "        group file names derive\n\n"
        "Returns:\n"
        "    nothing; raises when the key has no path or a file fails to write");
      // write(root_sink, group_sinks) — serialize into output buffers, one per group
      nb::cpp_function(
        [](nb::handle self, nb::object dest, nb::object groups)
        {
          wmoDispatch(self, [&](auto& mutableWmo)
          {
            const auto& wmo = std::as_const(mutableWmo);
            if (nb::len(groups) != wmo.groups.size())
              throw nb::value_error("write() needs exactly one output per group file");
            auto rootBytes = wmo.root.write();
            if (!rootBytes)
              throw wowlib::ResultError(rootBytes.error());
            dest.attr("write")(toPybytes(*rootBytes));
            for (std::size_t i = 0; i < wmo.groups.size(); ++i)
            {
              auto groupBytes = wmo.groups[i].write();
              if (!groupBytes)
                throw wowlib::ResultError(groupBytes.error());
              nb::object(groups[i]).attr("write")(toPybytes(*groupBytes));
            }
          });
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("groups"),
        nb::sig("def write(self, dest: typing.BinaryIO, "
                "groups: collections.abc.Sequence[typing.BinaryIO]) -> None"),
        "Serialize into binary sinks: the root into dest, each group into its\n"
        "own sink — exactly one per group, in group order.\n\n"
        "Args:\n"
        "    dest (BinaryIO): where the root file bytes are written\n"
        "    groups (Sequence[BinaryIO]): one binary sink per group file, ordered\n\n"
        "Returns:\n"
        "    nothing; raises when the sink count mismatches the group count");

      // convert(target) — Literal per target (narrows) + Expansion -> AnyWMO fallback
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
              result = nb::cast(convertWmoFromAny<to>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyWMO"),
        "Rebuild this assembly as the target expansion's concrete class,\n"
        "stepping the version ladder one adjacent release at a time (this\n"
        "instance is left unchanged). The return type narrows when the target\n"
        "is a literal.\n\n"
        "Args:\n"
        "    target: the expansion to convert to\n\n"
        "Returns:\n"
        "    the converted assembly; raises when a ladder step is not implemented");
    }
  }

  void registerFacade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ wmo = nb::cast<nb::module_>(formats.attr("wmo"));
    nb::module_ root = nb::cast<nb::module_>(wmo.attr("root"));
    nb::module_ group = nb::cast<nb::module_>(wmo.attr("group"));
    nb::module_ groupChunks = nb::cast<nb::module_>(group.attr("chunks"));

    // every family hands the facade its canonicalization pivots + grid, so
    // the Literal overload/sig names are the same range-suffixed classes the
    // welded alias tables produce
    namespace fwmo = wowlib::formats::wmo;
    defForVersion<fwmo::WMO>(wmo.attr("WMO"), "WMO", fwmo::WmoAssemblyPivots,
                               fwmo::WmoVersions);
    defForVersion<fwmo::root::WMORoot>(root.attr("WMORoot"), "WMORoot",
                                         fwmo::WmoRootPivots, fwmo::WmoVersions);
    defForVersion<fwmo::group::WMOGroup>(group.attr("WMOGroup"), "WMOGroup",
                                           fwmo::WmoGroupPivots, fwmo::WmoVersions);
    defForVersion<fwmo::group::WMOGroupBody>(group.attr("WMOGroupBody"), "WMOGroupBody",
                                               fwmo::WmoGroupPivots, fwmo::WmoVersions);
    defForVersion<fwmo::group::chunks::SMOGroupHeader>(
      groupChunks.attr("WMOGroupHeader"), "WMOGroupHeader", fwmo::WmoGroupHeaderPivots,
      fwmo::WmoVersions);
    defForVersion<fwmo::group::chunks::SMOBatch>(
      groupChunks.attr("WMOBatch"), "WMOBatch", fwmo::WmoBatchPivots, fwmo::WmoVersions);

    defWmoOps(wmo.attr("WMO"));

    // Runtime AnyX union aliases (importable TypeAliases; stubgen renders them
    // natively — see defAnyAlias). Each lands on the submodule that owns its
    // concretes. A new Expansion grows every union with no edit here.
    // the abstract bases speak the validation verbs too, so code annotated
    // against the family (def check(w: WMO)) type-checks (see facade.hpp)
    defValidationVerbs<fwmo::WMO>(wmo.attr("WMO"), "WMO");
    defValidationVerbs<fwmo::root::WMORoot>(root.attr("WMORoot"), "WMORoot");
    defValidationVerbs<fwmo::group::WMOGroup>(group.attr("WMOGroup"), "WMOGroup");
    defValidationVerbs<fwmo::group::WMOGroupBody>(group.attr("WMOGroupBody"),
                                                    "WMOGroupBody");

    defAnyAlias<fwmo::WMO>(wmo, "WMO", fwmo::WmoAssemblyPivots, fwmo::WmoVersions);
    defAnyAlias<fwmo::root::WMORoot>(root, "WMORoot", fwmo::WmoRootPivots,
                                       fwmo::WmoVersions);
    defAnyAlias<fwmo::group::WMOGroup>(group, "WMOGroup", fwmo::WmoGroupPivots,
                                         fwmo::WmoVersions);
    defAnyAlias<fwmo::group::WMOGroupBody>(group, "WMOGroupBody", fwmo::WmoGroupPivots,
                                             fwmo::WmoVersions);
    defAnyAlias<fwmo::group::chunks::SMOGroupHeader>(groupChunks, "WMOGroupHeader",
                                                       fwmo::WmoGroupHeaderPivots,
                                                       fwmo::WmoVersions);
    defAnyAlias<fwmo::group::chunks::SMOBatch>(groupChunks, "WMOBatch",
                                                 fwmo::WmoBatchPivots, fwmo::WmoVersions);
  }
}
