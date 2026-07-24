#pragma once

/** @file
    M2 version conversion (namespace wowlib::formats): the M2's contribution
    to the generic convert<To>() ladder (formats/convert.hpp) — its
    supported_versions list and, as they are written, the adjacent-version
    convert_step overloads translating one era's layout to the next.

    Include this (rather than m2.hpp) wherever you convert an M2 across
    versions or query the ladder (has_convert_path / convert). */

#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/m2/m2.hpp>

namespace wowlib::formats
{
  /** The M2's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see m2::m2_versions). convert<To>() walks
      this one adjacent step at a time. */
  template <>
  inline constexpr auto supported_versions<m2::M2> = m2::m2_versions;

  // convert_step(const m2::M2<From>&, version_tag<To>) overloads go here as
  // each adjacent-version pair's layout translation is implemented (track
  // timeline splitting/merging, skin embedding/extraction, particle record
  // migration); until then only identity conversion succeeds.
}
