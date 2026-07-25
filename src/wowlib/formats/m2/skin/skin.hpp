#pragma once

/** @file
    The external .skin file entity (namespace wowlib::formats::m2::skin), WotLK+:
    the 'SKIN' magic followed by one M2SkinProfile — held as an inline member,
    so the wire layout is byte-identical to the embedded pre-WotLK profiles
    while the entity keeps a single welded facade base. Offsets are relative
    to the .skin file itself; the referenced vertices stay in the .m2. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/offsets.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/skin/records.hpp>

namespace wowlib::formats::m2::skin
{
  /** The .skin leading magic, as memcpy'd from disk. */
  inline constexpr std::uint32_t skin_magic = 0x4E494B53;  // "SKIN"

  /** The version-agnostic base of every Skin<V> (welded as "Skin").

      This empty base exists ENTIRELY for the language bindings: it gives the
      per-version Skin* classes a common welded supertype so binding users can
      write version-agnostic code (isinstance, Skin.for_version(expansion)).
      It has no role in the C++ API, where you use the concrete Skin<V>.

      @see https://wowdev.wiki/M2/.skin */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("Skin"),
    =welder::doc(R"(
        An external model LOD view (.skin file), abstract over the client
        version. Construct a concrete version with Skin.for_version(expansion);
        the per-version Skin* classes are subclasses. See
        https://wowdev.wiki/M2/.skin.)")
  ]] SkinBase
  {
    bool operator==(const SkinBase&) const = default;
  };

  /** One external LOD view of a model ("{model}0N.skin", or SFID FileDataIDs
      in Legion+). Only exists WotLK+ — earlier clients embed the profiles in
      the MD20 header (M2Root.skin_profiles), with the identical layout minus
      the magic.
      @tparam V the client version this skin targets.
      @see https://wowdev.wiki/M2/.skin */
  namespace detail
  {
  template <ClientVersion V>
    requires (V >= m2_per_sequence_timelines)
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One external LOD view (.skin file, WotLK+): the 'SKIN' magic plus the
        profile tables (local lookups, submeshes, render batches). See
        https://wowdev.wiki/M2/.skin.)")
  ]] Skin : OffsetFile<Skin<V>>, SkinBase
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::exclude,
      =welder::doc("The leading magic, 'SKIN' — constant on every view file; "
                   "hidden from the bindings.")]]
    std::uint32_t magic = skin_magic;

    [[=welder::doc("The LOD view's tables (local lookups, submeshes, "
                   "batches).")]]
    skin::M2SkinProfile<V> profile{};

    bool operator==(const Skin&) const = default;
  };
  }

  /** An external LOD view — the canonicalizing face of detail::Skin: every
      WotLK+ version maps to its range's first grid version (m2_skin_pivots:
      only Cata's shadow batches split the era, so two instantiations cover
      all nine releases). Pre-WotLK versions stay a substitution failure. */
  template <ClientVersion V>
    requires (V >= m2_per_sequence_timelines)
  using Skin =
    detail::Skin<canonical_version(V, m2_skin_pivots, m2_skin_versions)>;
}
