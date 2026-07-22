#pragma once

/** @file
    @brief wowlib's Python naming policy for welder's reflection-driven module walk.

    welder styles every welded identifier through a @c name_style. wowlib wants
    PEP 8 for the things Python programmers spell in @c snake_case (callables, data
    members, submodules) but the *type* vocabulary must survive verbatim: wowlib's
    C++ class, enum and enumerator names are the game client's own canonical
    spellings (@c SMOHeader, @c WMORoot, @c FileDataID), and welder's CapWords
    normalization would corrupt the embedded acronyms (@c SmoHeader, @c WmoRoot,
    @c FileDataId). */

#include <meta>
#include <string>

#include <welder/naming.hpp>

/** @namespace wowlib_py
    @brief Hand-written glue for the wowlib Python extension.

    This is a plain organizational namespace for the binding translation units;
    it is deliberately *not* nested inside @c wowlib so welder's module walk (which
    enumerates the members of @c ::wowlib) never mistakes the glue for a submodule
    to bind. */
namespace wowlib_py
{
  /** @brief PEP 8 for callables/data, verbatim for the type vocabulary.

      Inherits welder's @c snake_case styling and overrides only the three
      transforms that would otherwise mangle client-canonical type names, keeping
      them byte-for-byte identical to their C++ identifiers. */
  struct wowlib_python_naming : welder::naming::snake_case
  {
    /** @brief Bind class/struct identifiers verbatim (no CapWords normalization). */
    static consteval std::string transform_class(std::meta::info e)
    {
      return std::string{std::meta::identifier_of(e)};
    }

    /** @brief Bind enum-type identifiers verbatim. */
    static consteval std::string transform_enum(std::meta::info e)
    {
      return std::string{std::meta::identifier_of(e)};
    }

    /** @brief Bind enumerator identifiers verbatim (e.g. @c Locale.enUS). */
    static consteval std::string transform_enumerator(std::meta::info e)
    {
      return std::string{std::meta::identifier_of(e)};
    }
  };

  static_assert(welder::naming::name_style<wowlib_python_naming>);
}
