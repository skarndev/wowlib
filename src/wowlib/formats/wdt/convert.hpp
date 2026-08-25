#pragma once

/** @file
    WDT version conversion (namespace wowlib::formats): the WDT's contribution
    to the generic convert<To>() ladder (formats/convert.hpp) — its
    supported_versions list and, as they are written, the adjacent-version
    convert_step overloads.

    Include this (rather than wdt.hpp) wherever you convert a WDT across
    versions or query the ladder (has_convert_path / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

namespace wowlib::formats {
  /** The WDT's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see wdt::wdt_versions). convert<To>() walks
      this one adjacent step at a time. */
  template <>
  inline constexpr auto supported_versions<wdt::WDT> = wdt::wdt_versions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the
      family from an entity reference, and deduction sees wdt::detail::WDT —
      an alias template is not identity-equal to its target for
      template-template matching. */
  template <>
  inline constexpr auto supported_versions<wdt::detail::WDT> = wdt::wdt_versions;

  /** The assembly's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto version_pivots<wdt::WDT> = wdt::wdt_assembly_pivots;
  template <>
  inline constexpr auto version_pivots<wdt::detail::WDT> = wdt::wdt_assembly_pivots;

  // convert_step(const wdt::WDT<From>&, version_tag<To>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until
  // then only identity conversion (From == To) succeeds, and has_convert_path()
  // reports every non-identity pair as unavailable.
}
