#pragma once

/** @file
    WDT version conversion (namespace wowlib::formats): the WDT's contribution
    to the generic convert<to>() ladder (formats/convert.hpp) — its
    SupportedVersions list and, as they are written, the adjacent-version
    convert_step overloads.

    Include this (rather than wdt.hpp) wherever you convert a WDT across
    versions or query the ladder (hasConvertPath / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

namespace wowlib::formats {
  /** The WDT's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see wdt::WdtVersions). convert<to>() walks
      this one adjacent step at a time. */
  template <>
  inline constexpr auto SupportedVersions<wdt::WDT> = wdt::WdtVersions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the
      family from an entity reference, and deduction sees wdt::detail::WDT —
      an alias template is not identity-equal to its target for
      template-template matching. */
  template <>
  inline constexpr auto SupportedVersions<wdt::detail::WDT> = wdt::WdtVersions;

  /** The assembly's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto VersionPivots<wdt::WDT> = wdt::WdtAssemblyPivots;
  template <>
  inline constexpr auto VersionPivots<wdt::detail::WDT> = wdt::WdtAssemblyPivots;

  // convert_step(const wdt::WDT<from>&, VersionTag<to>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until
  // then only identity conversion (from == to) succeeds, and hasConvertPath()
  // reports every non-identity pair as unavailable.
}
