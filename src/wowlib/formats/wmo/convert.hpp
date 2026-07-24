#pragma once

/** @file
    WMO version conversion (namespace wowlib::formats): the WMO's contribution to
    the generic convert<To>() ladder (formats/convert.hpp) — its
    supported_versions list and, as they are written, the adjacent-version
    convert_step overloads that translate one era's chunk-set changes to the next.

    Include this (rather than wmo.hpp) wherever you convert a WMO across versions
    or query the ladder (has_convert_path / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wmo/wmo.hpp>

namespace wowlib::formats
{
  /** The WMO's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see wmo::wmo_versions). convert<To>() walks this
      one adjacent step at a time. */
  template <>
  inline constexpr auto supported_versions<wmo::WMO> = wmo::wmo_versions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the
      family from an entity reference, and deduction sees wmo::detail::WMO —
      an alias template is not identity-equal to its target for
      template-template matching. */
  template <>
  inline constexpr auto supported_versions<wmo::detail::WMO> = wmo::wmo_versions;

  /** The assembly's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto version_pivots<wmo::WMO> = wmo::wmo_assembly_pivots;
  template <>
  inline constexpr auto version_pivots<wmo::detail::WMO> = wmo::wmo_assembly_pivots;

  // convert_step(const wmo::WMO<From>&, version_tag<To>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until then
  // only identity conversion (From == To) succeeds, and has_convert_path() reports
  // every non-identity pair as unavailable.
}
