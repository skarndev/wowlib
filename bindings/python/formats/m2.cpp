/** @file
    @brief Implementation of the M2 versioned-format facade.

    See @c formats/m2.hpp for the surface and @c facade.hpp for the generic
    @c for_version machinery this reuses. The read/write/convert verbs live on
    the welded @c M2Base (Python only — Lua speaks the templates' own welded
    read/write via @c mark::only(lang::lua)); Skeleton gets its own read/write
    since .skel files are shared between models and serialize standalone.

    M2 has no buffer-parse overloads yet: an offset-format model pulls
    satellite files (.skin/.anim/.skel) during read, so the filesystem-gateway
    form is the only complete one — a span+satellites parse API is a planned
    follow-up. */

#include "formats/m2.hpp"

#include "instantiations/m2.hpp"

#include <format>
#include <string>

#include <wowlib/wowlib.hpp>

#include "facade.hpp"
#include "result_casters.hpp"

namespace wowlib_py::formats::m2
{
  namespace
  {
    /** Every expansion must have an @c m2_versions instantiation, or its facade
        overloads would name an unregistered class. Caught at compile time. */
    static_assert(
      []() consteval
      {
        for (auto e : std::meta::enumerators_of(^^wowlib::Expansion))
        {
          const auto version = wowlib::to_client_version(std::meta::extract<wowlib::Expansion>(e));
          bool ok = false;
          for (const auto& s : wowlib::formats::m2::m2_versions)
            ok = ok || s == version;
          if (!ok)
            return false;
        }
        return true;
      }(),
      "every Expansion enumerator needs an m2_versions instantiation for its facade");

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

    /** @brief Dispatch @p fn to @p self's concrete @c F<X> via isinstance,
        skipping the expansions a subset family excludes.
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

    /** @brief Convert @p source (any @c M2<From>) to target expansion @p To.

        Identifies the source version by isinstance, then composes the C++
        convert ladder; a pair with no complete @c convert_step ladder degrades
        to a @c NotImplemented Result.
        @throws nanobind::type_error if @p source is not an M2 instance. */
    template <wowlib::Expansion To>
    wowlib::Result<typename concrete_of<wowlib::formats::m2::M2, To>::type>
    convert_m2_from_any(nb::handle source)
    {
      template for (constexpr auto e : expansion_enumerators)
      {
        constexpr wowlib::Expansion From = [:e:];
        using S = wowlib::formats::m2::M2<wowlib::to_client_version(From)>;
        if (nb::isinstance<S>(source))
        {
          if constexpr (wowlib::formats::has_convert_path<wowlib::formats::m2::M2,
                                                          wowlib::to_client_version(From),
                                                          wowlib::to_client_version(To)>())
            return wowlib::formats::convert<wowlib::to_client_version(To)>(
              nb::cast<const S&>(source));
          else
            return wowlib::make_error(
              wowlib::ErrorCode::NotImplemented,
              std::format("M2 conversion {} -> {} has no complete convert_step ladder yet",
                          wowlib::enum_name(From), wowlib::enum_name(To)));
        }
      }
      throw nb::type_error("convert() expects an M2 instance");
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
          return nb::cast(convert_m2_from_any<To>(self));
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig(persist("def convert(self, target: typing.Literal[wowlib.Expansion."
                        + std::string{wowlib::enum_name(To)} + "]) -> "
                        + concrete_name("M2", To, wowlib::formats::m2::m2_assembly_pivots,
                                        wowlib::formats::m2::m2_versions))));
    }

    /** @brief Attach @c read/@c write/@c convert to @c M2Base.

        read/write speak the (FileSystem, FileKey) pair — the model pulls its
        satellite files (.skin/.anim/.skel/.bone/.phys) through the gateway
        and re-splits them on write. @c convert narrows on its target Literal
        with an @c Expansion → @c AnyM2 fallback. */
    void def_m2_ops(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          family_dispatch<wowlib::formats::m2::M2>(
            self,
            [&](auto& model)
            {
              if (auto r = model.read(fs, key); !r)
                throw wowlib::result_error(r.error());
            },
            "expected an M2 instance");
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("key"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));

      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          family_dispatch<wowlib::formats::m2::M2>(
            self,
            [&](auto& model)
            {
              if (auto r = std::as_const(model).write(fs, key); !r)
                throw wowlib::result_error(r.error());
            },
            "expected an M2 instance");
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("key"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));

      // convert(target) — Literal per target (narrows) + Expansion -> AnyM2 fallback
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
              result = nb::cast(convert_m2_from_any<To>(self));
              found = true;
            }
          }
          if (!found)
            throw nb::value_error("no wowlib instantiation for that target expansion");
          return result;
        },
        nb::name("convert"), nb::scope(base), nb::is_method(), nb::arg("target"),
        nb::sig("def convert(self, target: wowlib.Expansion) -> AnyM2"));
    }

    /** @brief Attach @c read/@c write to @c SkeletonBase — skeletons are
        first-class shared entities with their own filesystem verbs (the
        chunk-level read(bytes)/write() pair welds on the concretes). */
    void def_skeleton_ops(nb::handle base)
    {
      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          family_dispatch<wowlib::formats::m2::Skeleton>(
            self,
            [&](auto& skel)
            {
              if (auto r = skel.read(fs, key); !r)
                throw wowlib::result_error(r.error());
            },
            "expected a Skeleton instance");
        },
        nb::name("read"), nb::scope(base), nb::is_method(),
        nb::arg("source"), nb::arg("key"),
        nb::sig("def read(self, source: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));

      nb::cpp_function(
        [](nb::handle self, wowlib::fs::FileSystem& fs, const wowlib::FileKey& key)
        {
          family_dispatch<wowlib::formats::m2::Skeleton>(
            self,
            [&](auto& skel)
            {
              if (auto r = std::as_const(skel).write(fs, key); !r)
                throw wowlib::result_error(r.error());
            },
            "expected a Skeleton instance");
        },
        nb::name("write"), nb::scope(base), nb::is_method(),
        nb::arg("dest"), nb::arg("key"),
        nb::sig("def write(self, dest: wowlib.fs.FileSystem, key: wowlib.FileKey) -> None"));
    }
  }

  void register_facade(nb::module_& module)
  {
    nb::module_ formats = nb::cast<nb::module_>(module.attr("formats"));
    nb::module_ m2 = nb::cast<nb::module_>(formats.attr("m2"));
    // the per-family submodules mirror the C++ namespaces (m2::root,
    // m2::chunked, m2::skin)
    nb::module_ root = nb::cast<nb::module_>(m2.attr("root"));
    nb::module_ chunked = nb::cast<nb::module_>(m2.attr("chunked"));
    nb::module_ skin = nb::cast<nb::module_>(m2.attr("skin"));

    // every family hands the facade its canonicalization pivots + grid, so
    // the Literal overload/sig names are the same range-suffixed classes the
    // welded alias tables produce
    namespace fm2 = wowlib::formats::m2;
    def_for_version<fm2::M2>(m2.attr("M2"), "M2", fm2::m2_assembly_pivots, fm2::m2_versions);
    def_for_version<fm2::M2Root>(root.attr("M2Root"), "M2Root", fm2::m2_data_pivots,
                                 fm2::m2_versions);
    def_for_version<fm2::Skin>(skin.attr("Skin"), "Skin", fm2::m2_skin_pivots,
                               fm2::m2_skin_versions);
    def_for_version<fm2::M2ChunkedFile>(chunked.attr("M2ChunkedFile"), "M2ChunkedFile",
                                        fm2::m2_file_pivots, fm2::m2_chunked_versions);
    def_for_version<fm2::Skeleton>(m2.attr("Skeleton"), "Skeleton", fm2::m2_skeleton_pivots,
                                   fm2::m2_chunked_versions);

    def_m2_ops(m2.attr("M2"));
    def_skeleton_ops(m2.attr("Skeleton"));

    // Runtime AnyX union aliases (importable TypeAliases) on the family's own
    // submodule; the subset families fold only the expansions they exist for.
    def_any_alias<fm2::M2>(m2, "M2", fm2::m2_assembly_pivots, fm2::m2_versions);
    def_any_alias<fm2::M2Root>(root, "M2Root", fm2::m2_data_pivots, fm2::m2_versions);
    def_any_alias<fm2::Skin>(skin, "Skin", fm2::m2_skin_pivots, fm2::m2_skin_versions);
    def_any_alias<fm2::M2ChunkedFile>(chunked, "M2ChunkedFile", fm2::m2_file_pivots,
                                      fm2::m2_chunked_versions);
    def_any_alias<fm2::Skeleton>(m2, "Skeleton", fm2::m2_skeleton_pivots,
                                 fm2::m2_chunked_versions);
  }
}
