#pragma once

/** @file
    WMO version conversion (namespace wowlib::formats): the WMO's contribution to
    the generic convert<to>() ladder (formats/convert.hpp) — its
    SupportedVersions list and, as they are written, the adjacent-version
    convert_step overloads that translate one era's chunk-set changes to the next.

    Include this (rather than wmo.hpp) wherever you convert a WMO across versions
    or query the ladder (hasConvertPath / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wmo/wmo.hpp>

namespace wowlib::formats {
  /** The WMO's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see wmo::WmoVersions). convert<to>() walks this
      one adjacent step at a time. */
  template <>
  inline constexpr auto SupportedVersions<wmo::WMO> = wmo::WmoVersions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the
      family from an entity reference, and deduction sees wmo::detail::WMO —
      an alias template is not identity-equal to its target for
      template-template matching. */
  template <>
  inline constexpr auto SupportedVersions<wmo::detail::WMO> = wmo::WmoVersions;

  /** The assembly's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto VersionPivots<wmo::WMO> = wmo::WmoAssemblyPivots;
  template <>
  inline constexpr auto VersionPivots<wmo::detail::WMO> = wmo::WmoAssemblyPivots;

  // convert_step(const wmo::WMO<from>&, VersionTag<to>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until then
  // only identity conversion (from == to) succeeds, and hasConvertPath() reports
  // every non-identity pair as unavailable.
}
