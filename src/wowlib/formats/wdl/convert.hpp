#pragma once

/** @file
    WDL version conversion (namespace wowlib::formats): the WDL's contribution
    to the generic convert<To>() ladder (formats/convert.hpp) — its
    supported_versions list and, as they are written, the adjacent-version
    convert_step overloads.

    Include this (rather than wdl.hpp) wherever you convert a WDL across
    versions or query the ladder (has_convert_path / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wdl/wdl.hpp>

namespace wowlib::formats {
  /** The WDL's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see wdl::wdl_versions). convert<To>() walks
      this one adjacent step at a time. */
  template <>
  inline constexpr auto supported_versions<wdl::WDL> = wdl::wdl_versions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the
      family from an entity reference, and deduction sees wdl::detail::WDL —
      an alias template is not identity-equal to its target for
      template-template matching. */
  template <>
  inline constexpr auto supported_versions<wdl::detail::WDL> = wdl::wdl_versions;

  /** The entity's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto version_pivots<wdl::WDL> = wdl::wdl_pivots;
  template <>
  inline constexpr auto version_pivots<wdl::detail::WDL> = wdl::wdl_pivots;

  // convert_step(const wdl::WDL<From>&, version_tag<To>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until
  // then only identity conversion (From == To) succeeds, and has_convert_path()
  // reports every non-identity pair as unavailable.
}
