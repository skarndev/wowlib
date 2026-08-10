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

#include <span>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/expansion.hpp>
#include <wowlib/core/reflect.hpp>
#include <wowlib/formats/common/validation.hpp>
#include <wowlib/formats/common/version_range.hpp>

#include "result_casters.hpp"

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

  /** @brief The RANGE-suffixed concrete class name expansion @p x maps to,
      e.g. @c "M2" + @c Mop → @c "M2CataToMop": the family instantiates one
      class per canonical version range (see version_range.hpp), and every
      facade spelling goes through the same suffix derivation the welded
      alias tables are checked against. */
  inline std::string concrete_name(std::string_view base, wowlib::Expansion x,
                                   std::span<const wowlib::ClientVersion> pivots,
                                   std::span<const wowlib::ClientVersion> grid)
  {
    const wowlib::ClientVersion canonical =
      wowlib::formats::canonical_version(wowlib::to_client_version(x), pivots, grid);
    return std::string{base} + wowlib::formats::range_suffix(canonical, pivots, grid);
  }

  /** @brief Whether family @p F instantiates for expansion @p X.

      Constrained families exclude early eras (Skin is WotLK+, M2ChunkedFile and
      Skeleton are Legion+); naming an excluded specialization inside the
      requires-expression is a substitution failure, not an error. Every
      facade walk guards on this so subset families skip the missing
      expansions instead of tripping their constraints. */
  template <template <wowlib::ClientVersion> class F, wowlib::Expansion X>
  concept family_has = requires { typename F<wowlib::to_client_version(X)>; };

  /** @brief The concrete (canonical) instantiation family @p F maps expansion
      @p X to, as a nested typedef. Function templates that need it in their
      SIGNATURE must go through this indirection: spelling the canonicalizing
      alias there directly trips gcc 16's "sorry, unimplemented: mangling
      view_convert_expr" (the span-converting canonical_version call cannot be
      mangled as a dependent expression). */
  template <template <wowlib::ClientVersion> class F, wowlib::Expansion X>
  struct concrete_of
  {
    using type = F<wowlib::to_client_version(X)>;
  };

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
      if constexpr (family_has<F, X>)
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

  /** @brief Attach one @c for_version Literal overload (@p X → its range's
      concrete class; several Literals may share one class). */
  template <template <wowlib::ClientVersion> class F, wowlib::Expansion X>
  void def_for_version_overload(nb::handle base, std::string_view base_name,
                                std::span<const wowlib::ClientVersion> pivots,
                                std::span<const wowlib::ClientVersion> grid)
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
                      + concrete_name(base_name, X, pivots, grid))));
  }

  /** @brief Attach @c for_version to a family @p base.

      Emits one @c Literal overload per expansion (mypy narrows each to the concrete
      class) plus an @c Expansion → @c AnyX runtime fallback.

      @tparam F the family class template.
      @param base the welded family base to receive the static method.
      @param base_name the base's Python name, used to spell the @c concrete/@c AnyX
             return types in the generated signatures.
      @param pivots the family's canonicalization pivots (boundaries header).
      @param grid   the family's release grid (full or era subset). */
  template <template <wowlib::ClientVersion> class F>
  void def_for_version(nb::handle base, std::string_view base_name,
                       std::span<const wowlib::ClientVersion> pivots,
                       std::span<const wowlib::ClientVersion> grid)
  {
    template for (constexpr auto e : expansion_enumerators)
      if constexpr (family_has<F, ([:e:])>)
        def_for_version_overload<F, ([:e:])>(base, base_name, pivots, grid);
    nb::cpp_function(
      [](wowlib::Expansion expansion) { return make_for_version<F>(expansion); },
      nb::name("for_version"), nb::scope(base), nb::arg("expansion"),
      nb::sig(persist("def for_version(expansion: wowlib.Expansion) -> Any"
                      + std::string{base_name})));
  }

  /** @brief Run @p fn against @p self cast to concrete @c F<X>, if it is one.
      @return true when @p self was an @c F<X> and @p fn ran. */
  template <template <wowlib::ClientVersion> class F, wowlib::Expansion X, typename Fn>
  bool family_try(nb::handle self, Fn&& fn)
  {
    if constexpr (family_has<F, X>)
    {
      using Concrete = typename concrete_of<F, X>::type;
      if (nb::isinstance<Concrete>(self))
      {
        fn(nb::cast<Concrete&>(self));
        return true;
      }
    }
    return false;
  }

  /** @brief Dispatch @p fn to @p self's concrete @c F<X> via isinstance.

      The format-agnostic twin of each facade TU's hand-written dispatcher, for
      verbs that need no per-version signature narrowing. Expansions sharing a
      canonical range resolve to the same class, so the first isinstance hit is
      the right one.
      @param self the instance the verb was called on.
      @param base_name the family base name, for the type error.
      @param fn the callable to run against the concrete reference.
      @throws nanobind::type_error when @p self is not of this family. */
  template <template <wowlib::ClientVersion> class F, typename Fn>
  void family_dispatch(nb::handle self, std::string_view base_name, Fn&& fn)
  {
    bool done = false;
    template for (constexpr auto e : expansion_enumerators)
      if (!done)
        done = family_try<F, ([:e:])>(self, fn);
    if (!done)
      throw nb::type_error(persist("expected a " + std::string{base_name} + " instance"));
  }

  /** @brief Bind @c validate / @c ensure_valid on a family's abstract base.

      welder already binds both on every CONCRETE class (they are plain members
      of the entity), which is enough to CALL them. This adds them to the base
      so that code annotated against the abstract family — @c def @c check(w:
      @c WMO) — type-checks, exactly as @c read / @c write already do. The
      concrete's own binding shadows this one at runtime; both dispatch to the
      same C++ method, so which wins does not matter.
      @tparam F the family class template.
      @param base the family's welded base handle.
      @param base_name the family base name, e.g. @c "WMO". */
  /** @brief Whether any of family @p F's concretes carries @c validate().

      Binary-struct families (@c WMOBatch, @c WDTHeader) and the entities that
      have no contracts of their own do not, so the verbs must not be bound for
      them — a base method that can only ever raise is worse than an absent one.
      Lets every facade call @c def_validation_verbs unconditionally. */
  template <template <wowlib::ClientVersion> class F>
  consteval bool family_validates()
  {
    bool any = false;
    template for (constexpr auto e : expansion_enumerators)
      if constexpr (family_has<F, ([:e:])>)
        if constexpr (requires(const typename concrete_of<F, ([:e:])>::type& x) { x.validate(); })
          any = true;
    return any;
  }

  template <template <wowlib::ClientVersion> class F>
  void def_validation_verbs(nb::handle base, std::string_view base_name)
  {
    if constexpr (!family_validates<F>())
      return;
    else
    {
    const char* name = persist(std::string{base_name});
    nb::cpp_function(
      [name](nb::handle self)
      {
        wowlib::formats::ValidationReport report;
        family_dispatch<F>(self, name, [&](auto& entity) { report = entity.validate(); });
        return report;
      },
      nb::name("validate"), nb::scope(base), nb::is_method(),
      nb::sig("def validate(self) -> wowlib.formats.ValidationReport"),
      "Check the logical integrity contracts this file must satisfy to LOAD in\n"
      "the client, which write() deliberately never enforces. Call it before\n"
      "writing when you want to know the result will load. A file read from a\n"
      "client and left unmodified reports no errors; warnings mark states real\n"
      "client files ship.\n\n"
      "Returns:\n"
      "    every violated contract, each with its member path");
    nb::cpp_function(
      [name](nb::handle self)
      {
        family_dispatch<F>(self, name, [&](auto& entity)
        {
          if (auto r = entity.ensure_valid(); !r)
            throw wowlib::result_error(r.error());
        });
      },
      nb::name("ensure_valid"), nb::scope(base), nb::is_method(),
      nb::sig("def ensure_valid(self) -> None"),
      "Validate and raise on the first error instead of returning a report —\n"
      "the assert-style face of validate().\n\n"
      "Returns:\n"
      "    nothing; raises when validate() finds any error");
    }
  }

  /** @brief Build the runtime @c AnyX union alias and bind it on @p module.

      Folds the family's concrete classes — @c WMOVanilla @c | @c WMOTbc @c | ...
      — into a @c types.UnionType and binds it as @c module.Any<base_name>, so the
      alias is a REAL, importable object (@c from @c wowlib.formats.wmo @c import
      @c AnyWMO works; usable in annotations and, on 3.10+, in @c isinstance) and
      not merely a stub-only name. It is derived from the same expansion walk and
      @c concrete_name every other facade piece uses, so a new @c Expansion grows
      the union automatically. Because the bound value is a @c types.UnionType,
      nanobind's stubgen renders it as @c "AnyX: @c TypeAlias @c = @c WMOVanilla @c
      | @c ..." on its own — no PATTERN_FILE entry, one fewer coupling point.

      @tparam F the family class template — subset families (constrained to
              later eras) fold only the expansions they instantiate for, and
              expansions sharing a range fold the same class (a union
              deduplicates itself).
      @param module the submodule that owns the concrete classes and receives the
             alias (each family lives beside its own concretes).
      @param base_name the family base name, e.g. @c "WMO" → binds @c AnyWMO.
      @param pivots the family's canonicalization pivots.
      @param grid   the family's release grid. */
  template <template <wowlib::ClientVersion> class F>
  void def_any_alias(nb::module_ module, std::string_view base_name,
                     std::span<const wowlib::ClientVersion> pivots,
                     std::span<const wowlib::ClientVersion> grid)
  {
    nb::object alias;
    template for (constexpr auto e : expansion_enumerators)
    {
      if constexpr (family_has<F, ([:e:])>)
      {
        nb::object concrete =
          module.attr(concrete_name(base_name, [:e:], pivots, grid).c_str());
        alias = alias.is_valid() ? nb::object(alias | concrete) : concrete;
      }
    }
    module.attr(("Any" + std::string{base_name}).c_str()) = alias;
  }
}
