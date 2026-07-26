/** @file
    @brief Build-time generator for the opaque-container header (wowlib.opaque.hpp).

    welder's opaque-container rod reflects @c namespace @c wowlib, finds every STL
    container its welded types use, and emits the @c WELDER_OPAQUE declarations +
    welded aliases that bind those containers **by reference** (live mutation,
    @c append, zero-copy NumPy) instead of the default by-value copy. This TU is the
    whole generator executable — @c welder_generate_opaque_containers() (CMake) builds
    it and runs it into @c wowlib.opaque.hpp, which @c wowlib_module.cpp then includes
    before the module walk. No pybind11/nanobind here: pure reflection to text.

    The wrappers are named the wowlib way via a @c transform_opaque_container style
    hook (see below), so they read like the client types they wrap. */

#include <meta>
#include <string>

#include <welder/vocabulary.hpp>
#include <welder/naming.hpp>
#include <welder/rods/python/opaque_containers/module.hpp>

#include <wowlib/wowlib.hpp>

// The welded per-range alias tables live in the bindings, not the library —
// without them the walk sees no welded per-version entities and the
// generator would emit an (almost) empty header.
#include "instantiations/m2_ranges.hpp"
#include "instantiations/wdl_ranges.hpp"
#include "instantiations/wdt_ranges.hpp"
#include "instantiations/wmo_ranges.hpp"

namespace wowlib_py
{
  /** Names opaque container wrappers the wowlib way: `Vector` + the element's
      **verbatim** identifier — client acronyms are preserved exactly as the element
      binds (`SMOMaterial` → `VectorSMOMaterial`, `C3Vector` → `VectorC3Vector`), NOT
      restyled to `SmoMaterial`. For a versioned entity — a class template whose sole
      argument is a `ClientVersion` NTTP (`WMOGroup<V>`, `SMOBatch<V>`) — the arg maps
      to its Expansion spelling and suffixes the name: `WMOGroup<versions::wotlk>` →
      `VectorWMOGroupWotlk`. A fixed-size `std::array<T, N>` of a welded class keeps
      the same verbatim-element treatment with welder's extent token
      (`std::array<C3Vector, 3>` → `ArrayC3Vectorx3`, not the namespace-qualified
      default). Scalars, strings, maps and anything unrecognized fall straight
      through to welder's collision-free derived name (`VectorFloat`,
      `VectorShortUnsignedInt`, `ArrayShortx289`). The generator legalizes whatever
      we return. */
  struct wowlib_opaque_naming : welder::naming::none
  {
    /** Decimal render of @a n (constexpr std::to_string is unavailable on gcc-16). */
    static consteval std::string decimal(std::size_t n)
    {
      if (n == 0)
        return "0";
      std::string s;
      for (; n; n /= 10)
        s.insert(s.begin(), static_cast<char>('0' + n % 10));
      return s;
    }

    static consteval std::string transform_opaque_container(std::meta::info /*enclosing*/,
                                                            std::meta::info container,
                                                            std::meta::info /*member*/)
    {
      namespace oc = welder::rods::opaque_containers;
      const std::meta::info c = std::meta::dealias(container);

      if (!std::meta::has_template_arguments(c))
        return oc::derive_name(container);

      // Fixed-size sequences: Array + verbatim class-element identifier + x + extent
      // (scalar elements fall through — welder already derives ArrayShortx289).
      if (std::meta::template_of(c) == ^^std::array)
      {
        const auto args = std::meta::template_arguments_of(c);
        const std::meta::info elem = std::meta::dealias(args[0]);
        if (std::meta::is_type(elem) && std::meta::is_class_type(elem)
            && std::meta::has_identifier(elem) && !std::meta::has_template_arguments(elem))
          return "Array" + std::string{std::meta::identifier_of(elem)} + "x"
                 + decimal(std::meta::extract<std::size_t>(args[1]));
        return oc::derive_name(container);
      }

      // Growable sequences (std::vector); maps fall through.
      if (std::meta::template_of(c) != ^^std::vector)
        return oc::derive_name(container);

      const std::meta::info elem = std::meta::dealias(std::meta::template_arguments_of(c)[0]);
      if (!std::meta::is_type(elem) || !std::meta::is_class_type(elem))
        return oc::derive_name(container); // scalar / non-class element

      // Versioned entity: a class template parameterized solely by a ClientVersion NTTP.
      if (std::meta::has_template_arguments(elem))
      {
        const auto args = std::meta::template_arguments_of(elem);
        if (args.size() == 1 && !std::meta::is_type(args[0]) &&
            std::meta::remove_cv(std::meta::type_of(args[0])) == ^^wowlib::ClientVersion)
        {
          const auto version = std::meta::extract<wowlib::ClientVersion>(args[0]);
          if (const auto expansion = wowlib::to_expansion(version))
          {
            const std::string tmpl{std::meta::identifier_of(std::meta::template_of(elem))};
            const std::string ver{std::meta::identifier_of(std::meta::enumerators_of(
              ^^wowlib::Expansion)[static_cast<std::size_t>(*expansion)])};
            return "Vector" + tmpl + ver;
          }
        }
        return oc::derive_name(container); // some other class template
      }

      // A plain welded class: Vector + its verbatim identifier (acronyms intact).
      return "Vector" + std::string{std::meta::identifier_of(elem)};
    }
  };
}

WELDER_OPAQUE_CONTAINERS_MAIN_STYLED(wowlib, wowlib_py::wowlib_opaque_naming)
