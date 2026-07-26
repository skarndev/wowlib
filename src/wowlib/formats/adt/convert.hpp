#pragma once

/** @file
    ADT version conversion (namespace wowlib::formats): the ADT's contribution
    to the generic convert<To>() ladder (formats/convert.hpp) — its
    supported_versions list and, as they are written, the adjacent-version
    convert_step overloads. Include this (rather than adt.hpp) wherever you
    convert an ADT across versions or query the ladder. */

#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/convert.hpp>

namespace wowlib::formats
{
  /** The ADT's supported-version ladder: every targeted last-minor-of-major
      release, in release order (see adt::adt_versions). */
  template <>
  inline constexpr auto supported_versions<adt::ADT> = adt::adt_versions;
  /** The same ladder keyed on the detail template: convert() DEDUCES the family
      from an entity reference, and deduction sees adt::detail::ADT (an alias
      template is not identity-equal to its target for template-template match). */
  template <>
  inline constexpr auto supported_versions<adt::detail::ADT> = adt::adt_versions;

  /** The assembly's canonicalization pivots (both spellings, as above). */
  template <>
  inline constexpr auto version_pivots<adt::ADT> = adt::adt_pivots;
  template <>
  inline constexpr auto version_pivots<adt::detail::ADT> = adt::adt_pivots;

  // convert_step(const adt::ADT<From>&, version_tag<To>) overloads go here as
  // each adjacent-version pair's chunk-set translation is implemented; until
  // then only identity conversion (From == To) succeeds.
}
