#pragma once

/** @file
    WDL version conversion (namespace wowlib::formats): the WDL's contribution
    to the generic convert<to>() ladder (formats/convert.hpp) — its
    SupportedVersions list and, as they are written, the adjacent-version
    convert_step overloads.

    Include this (rather than wdl.hpp) wherever you convert a WDL across
    versions or query the ladder (hasConvertPath / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wdl/wdl.hpp>

namespace wowlib::formats {
  /** The WDL's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see wdl::WdlVersions). convert<to>() walks
      this one adjacent step at a time. */
  template <>
  inline constexpr auto SupportedVersions<wdl::WDL> = wdl::WdlVersions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the
      family from an entity reference, and deduction sees wdl::detail::WDL —
      an alias template is not identity-equal to its target for
      template-template matching. */
  template <>
  inline constexpr auto SupportedVersions<wdl::detail::WDL> = wdl::WdlVersions;

  /** The entity's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto VersionPivots<wdl::WDL> = wdl::WdlPivots;
  template <>
  inline constexpr auto VersionPivots<wdl::detail::WDL> = wdl::WdlPivots;

  // convert_step(const wdl::WDL<from>&, VersionTag<to>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until
  // then only identity conversion (from == to) succeeds, and hasConvertPath()
  // reports every non-identity pair as unavailable.
}
