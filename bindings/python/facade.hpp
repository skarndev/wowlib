#pragma once

/** @file
    @brief Generic machinery for wowlib's native versioned-format facade.

    Every version-differing format family is a class template @c F<ClientVersion>
    with a welded empty base (e.g. @c WMOBase, welded as @c WMO) that each
    instantiation inherits, so welder registers a *real* nanobind base and Python
    gets native inheritance and @c isinstance for free. What Python still needs, and
    what this header supplies, is a version-keyed constructor: @c for_version() is a
    set of native overloads attached to the family base — one @c Literal overload
    per @c Expansion (so mypy narrows @c Literal[Expansion.X] to the concrete class)
    plus a runtime @c Expansion → @c AnyX fallback.

    The helpers are format-agnostic and templated on the family template @c F, so a
    new format (ADT, M2, ...) reuses them verbatim; only the per-format assembly
    verbs (read/write/convert) live in a format-specific translation unit. */

#include <deque>
#include <string>
#include <string_view>

#include <meta>

#include <nanobind/nanobind.h>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/expansion.hpp>
#include <wowlib/core/reflect.hpp>

namespace wowlib_py
{
  namespace nb = nanobind;

  /** Every @c Expansion enumerator, materialized once for the template-for walks. */
  constexpr auto expansion_enumerators =
    std::define_static_array(std::meta::enumerators_of(^^wowlib::Expansion));

  /** @brief Intern a signature string so its address stays valid for the module.

      @c nb::sig stores the pointer it is handed rather than a copy, so a generated
      signature must live at a stable address for the module's lifetime. */
  inline const char* persist(std::string s)
  {
    static std::deque<std::string> store;
    return store.emplace_back(std::move(s)).c_str();
  }

  /** @brief Compose a concrete class name, e.g. @c "WMO" + @c Wotlk → @c "WMOWotlk". */
  inline std::string concrete_name(std::string_view base, wowlib::Expansion x)
  {
    return std::string{base} + std::string{wowlib::enum_name(x)};
  }

  /** @brief A bare, default-constructed @c F instance for expansion @p X.

      Hoisted out of the @c for_version loop bodies: gcc 16 refuses to instantiate
      @c F<...> from inside a lambda expanded within a @c template @c for. */
  template <template <wowlib::ClientVersion> class F, wowlib::Expansion X>
  nb::object make_one()
  {
    return nb::cast(F<wowlib::to_client_version(X)>{});
  }

  /** @brief The @c for_version fallback: runtime-dispatch @p expansion to @c make_one. */
  template <template <wowlib::ClientVersion> class F>
  nb::object make_for_version(wowlib::Expansion expansion)
  {
    nb::object result;
    bool found = false;
    template for (constexpr auto e : expansion_enumerators)
    {
      constexpr wowlib::Expansion X = [:e:];
      if (!found && expansion == X)
      {
        result = make_one<F, ([:e:])>();
        found = true;
      }
    }
    if (!found)
      throw nb::value_error("no wowlib instantiation for that expansion");
    return result;
  }

  /** @brief Attach one @c for_version Literal overload (@p X → the concrete class). */
  template <template <wowlib::ClientVersion> class F, wowlib::Expansion X>
  void def_for_version_overload(nb::handle base, std::string_view base_name)
  {
    nb::cpp_function(
      [](wowlib::Expansion expansion) -> nb::object
      {
        if (expansion != X)
          throw nb::next_overload();
        return make_one<F, X>();
      },
      nb::name("for_version"), nb::scope(base), nb::arg("expansion"),
      nb::sig(persist("def for_version(expansion: typing.Literal[wowlib.Expansion."
                      + std::string{wowlib::enum_name(X)} + "]) -> "
                      + concrete_name(base_name, X))));
  }

  /** @brief Attach @c for_version to a family @p base.

      Emits one @c Literal overload per expansion (mypy narrows each to the concrete
      class) plus an @c Expansion → @c AnyX runtime fallback.

      @tparam F the family class template.
      @param base the welded family base to receive the static method.
      @param base_name the base's Python name, used to spell the @c concrete/@c AnyX
             return types in the generated signatures. */
  template <template <wowlib::ClientVersion> class F>
  void def_for_version(nb::handle base, std::string_view base_name)
  {
    template for (constexpr auto e : expansion_enumerators)
      def_for_version_overload<F, ([:e:])>(base, base_name);
    nb::cpp_function(
      [](wowlib::Expansion expansion) { return make_for_version<F>(expansion); },
      nb::name("for_version"), nb::scope(base), nb::arg("expansion"),
      nb::sig(persist("def for_version(expansion: wowlib.Expansion) -> Any"
                      + std::string{base_name})));
  }
}
