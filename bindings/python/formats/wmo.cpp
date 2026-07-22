/** @file
    @brief Implementation of the WMO versioned-format facade.

    See @c formats/wmo.hpp for the surface and @c facade.hpp for the generic
    @c for_version machinery this reuses. The read/write/convert verbs live on the
    welded @c WMOBase (Python only — Lua speaks the templates' own welded read/write
    via @c mark::only(lang::lua)), so per-expansion @c nb::sig overloads render as
    typed @c \@overload blocks and the concrete classes inherit them unchanged. */

#include "formats/wmo.hpp"

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
    /** Every expansion must have a @c wmo_versions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::to_client_version(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::wmo::wmo_versions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs a wmo_versions instantiation for its facade");

    /** @brief Run @p fn against @p self cast to its concrete @c WMO<X>, if it is one.
        @return true if @p self was a @c WMO<X> and @p fn ran. */
    template <wowlib::Expansion X, typename Fn>
    bool wmo_try(nb::handle self, Fn&& fn)
    {
      using W = wowlib::formats::wmo::WMO<wowlib::to_client_version(X)>;
      if (!nb::isinstance<W>(self))
        return false;
      fn(nb::cast<W&>(self));
      return true;
    }

    /** @brief Dispatch @p fn to @p self's concrete @c WMO<X> via isinstance.
        @throws nanobind::type_error if @p self is not a WMO instance. */
    template <typename Fn>
    void wmo_dispatch(nb::handle self, Fn&& fn)
    {
      bool done = false;
      template for (constexpr auto e : expansion_enumerators)
        if (!done)
          done = wmo_try<([:e:])>(self, fn);
      if (!done)
        throw nb::type_error("expected a WMO instance");
    }

    /** @brief Convert @p source (any @c WMO<From>) to target expansion @p To.

        Identifies the source version by isinstance, then composes the C++ convert
        ladder. A pair with no complete @c convert_step ladder degrades to a
        @c NotImplemented Result (identity always works; writing the C++ steps lights
        the pair up here with no glue change).
        @throws nanobind::type_error if @p source is not a WMO instance. */
    template <wowlib::Expansion To>
    wowlib::Result<wowlib::formats::wmo::WMO<wowlib::to_client_version(To)>>
    convert_wmo_from_any(nb::handle source)
    {
      template for (constexpr auto e : expansion_enumerators)
      {
        constexpr wowlib::Expansion From = [:e:];
        using S = wowlib::formats::wmo::WMO<wowlib::to_client_version(From)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::has_convert_path<wowlib::formats::wmo::WMO,
                                                          wowlib::to_client_version(From),
                                                          wowlib::to_client_version(To)>())
            return wowlib::formats::convert<wowlib::to_client_version(To)>(nb::cast<const S&>(source));
          else
            return wowlib::make_error(
              wowlib::ErrorCode::NotImplemented,
              std::format("WMO conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enum_name(From), wowlib::enum_name(To)));
        }
      }
      throw nb::type_error("convert() expects a WMO instance");
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
          return nb::cast(convert_wmo_from_any<To>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enum_name(To)} + "]) -> WMO"
                        + std::string{wowlib::enum_name(To)})));
    }

    /** @brief Attach @c read/@c write/@c convert to @c WMOBase.

        Each verb is a small set of native overloads (so stubgen renders typed
        @c \@overload blocks). @c read/@c write speak either a @c (FileSystem,
        FileKey) pair or in-memory buffers / binary file-likes; @c convert narrows
        on its target Literal with an @c Expansion → @c AnyWMO fallback. */
    void def_wmo_ops(nb::handle base)
    {
      // read(FileSystem, FileKey) — load in place
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          wmo_dispatch(self, [&](auto& wmo)
          {
            if (auto r = wmo.read(fs, key); !r)
              throw wowlib::result_error(r.error());
          });
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("key"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));
      // read(root_buffer, group_buffers) — parse in place from bytes / file-likes
      nb::cpp_function(
        [](nb::handle self, nb::object root, nb::object groups)
        {
          const wowlib::FileBuffer root_buffer = to_buffer(root);
          std::vector<wowlib::FileBuffer> group_buffers;
          const std::size_t n = nb::len(groups);
          group_buffers.reserve(n);
          for (std::size_t i = 0; i < n; ++i)
            group_buffers.push_back(to_buffer(nb::object(groups[i])));
          std::vector<std::span<const std::byte>> spans;
          spans.reserve(group_buffers.size());
          for (const auto& g : group_buffers)
            spans.emplace_back(g);
          wmo_dispatch(self, [&](auto& wmo)
          {
            if (auto r = wmo.read(std::span<const std::byte>{root_buffer}, std::span{spans}); !r)
              throw wowlib::result_error(r.error());
          });
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("groups"),
        nb::sig("def read(self, source: bytes | typing.BinaryIO, "
                "groups: collections.abc.Sequence[bytes | typing.BinaryIO]) -> None"));

      // write(FileSystem, FileKey) — save; group names inferred from the root key
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          wmo_dispatch(self, [&](auto& wmo)
          {
            if (auto r = std::as_const(wmo).write(fs, key); !r)
              throw wowlib::result_error(r.error());
          });
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("key"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));
      // write(root_sink, group_sinks) — serialize into output buffers, one per group
      nb::cpp_function(
        [](nb::handle self, nb::object dest, nb::object groups)
        {
          wmo_dispatch(self, [&](auto& mutable_wmo)
          {
            const auto& wmo = std::as_const(mutable_wmo);
            if (nb::len(groups) != wmo.groups.size())
              throw nb::value_error("write() needs exactly one output per group file");
            auto root_bytes = wmo.root.write();
            if (!root_bytes)
              throw wowlib::result_error(root_bytes.error());
            dest.attr("write")(to_pybytes(*root_bytes));
            for (std::size_t i = 0; i < wmo.groups.size(); ++i)
            {
              auto group_bytes = wmo.groups[i].write();
              if (!group_bytes)
                throw wowlib::result_error(group_bytes.error());
              nb::object(groups[i]).attr("write")(to_pybytes(*group_bytes));
            }
          });
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("groups"),
        nb::sig("def write(self, dest: typing.BinaryIO, "
                "groups: collections.abc.Sequence[typing.BinaryIO]) -> None"));

      // convert(target) — Literal per target (narrows) + Expansion -> AnyWMO fallback
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
              result = nb::cast(convert_wmo_from_any<To>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyWMO"));
    }
  }

  void register_facade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ wmo = nb::cast<nb::module_>(formats.attr("wmo"));
    nb::module_ root = nb::cast<nb::module_>(wmo.attr("root"));
    nb::module_ group = nb::cast<nb::module_>(wmo.attr("group"));
    nb::module_ group_chunks = nb::cast<nb::module_>(wmo.attr("group_chunks"));

    def_for_version<wowlib::formats::wmo::WMO>(wmo.attr("WMO"), "WMO");
    def_for_version<wowlib::formats::wmo::root::WMORoot>(root.attr("WMORoot"), "WMORoot");
    def_for_version<wowlib::formats::wmo::group::WMOGroup>(group.attr("WMOGroup"), "WMOGroup");
    def_for_version<wowlib::formats::wmo::group::WMOGroupBody>(group.attr("WMOGroupBody"),
                                                              "WMOGroupBody");
    def_for_version<wowlib::formats::wmo::group_chunks::SMOGroupHeader>(
      group_chunks.attr("WMOGroupHeader"), "WMOGroupHeader");
    def_for_version<wowlib::formats::wmo::group_chunks::SMOBatch>(
      group_chunks.attr("WMOBatch"), "WMOBatch");

    def_wmo_ops(wmo.attr("WMO"));
  }
}
