#pragma once

/** @file
    ADT version conversion (namespace wowlib::formats): the ADT's contribution
    to the generic convert<to>() ladder (formats/convert.hpp) — its
    SupportedVersions list and, as they are written, the adjacent-version
    convert_step overloads. Include this (rather than adt.hpp) wherever you
    convert an ADT across versions or query the ladder. */

#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/convert.hpp>

namespace wowlib::formats {
  /** The ADT's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see adt::AdtVersions). */
  template <>
  inline constexpr auto SupportedVersions<adt::ADT> = adt::AdtVersions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the family
      from an entity reference, and deduction sees adt::detail::ADT (an alias
      template is not identity-equal to its target for template-template match). */
  template <>
  inline constexpr auto SupportedVersions<adt::detail::ADT> = adt::AdtVersions;

  /** The assembly's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto VersionPivots<adt::ADT> = adt::AdtPivots;
  template <>
  inline constexpr auto VersionPivots<adt::detail::ADT> = adt::AdtPivots;

  // convert_step(const adt::ADT<from>&, VersionTag<to>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until
  // then only identity conversion (from == to) succeeds.
}
