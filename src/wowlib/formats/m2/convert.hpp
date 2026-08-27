#pragma once

/** @file
    M2 version conversion (namespace wowlib::formats): the M2's contribution
    to the generic convert<to>() ladder (formats/convert.hpp) — its
    SupportedVersions list and, as they are written, the adjacent-version
    convert_step overloads translating one era's layout to the next.

    Include this (rather than m2.hpp) wherever you convert an M2 across
    versions or query the ladder (hasConvertPath / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/m2/m2.hpp>

namespace wowlib::formats {
  /** The M2's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see m2::M2Versions). convert<to>() walks
      this one adjacent step at a time. */
  template <>
  inline constexpr auto SupportedVersions<m2::M2> = m2::M2Versions;
  /** The same ladder keyed on the detail template (see the WMO counterpart:
      deduction from an entity reference sees m2::detail::M2). */
  template <>
  inline constexpr auto SupportedVersions<m2::detail::M2> = m2::M2Versions;

  /** The assembly's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto VersionPivots<m2::M2> = m2::M2AssemblyPivots;
  template <>
  inline constexpr auto VersionPivots<m2::detail::M2> = m2::M2AssemblyPivots;

  // convert_step(const m2::M2<from>&, VersionTag<to>) overloads go here as
  // each adjacent-version pair's layout translation is implemented (track
  // timeline splitting/merging, skin embedding/extraction, particle record
  // migration); until then only identity conversion succeeds.
}
