#pragma once

/** @file
    The _fogs.wdt volumetric-fog satellite entity (namespace
    wowlib::formats::wdt::fogs), Legion 7.2.5+: placed fog volumes for terrain
    maps. The first Legion files ship empty (MVER only); content arrives with
    BfA 8.0.1. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>
#include <wowlib/formats/wdt/fogs/chunks/records.hpp>

namespace wowlib::formats::wdt::fogs
{
  using namespace wowlib::formats::wdt::fogs::chunks;

  /** The version-agnostic base of every WDTFogs<V> (welded as "WDTFogs");
      bindings-only, like every *Base.
      @see https://wowdev.wiki/WDT#fogs */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WDTFogs"),
    =welder::doc(R"(
        A _fogs.wdt volumetric-fog satellite (Legion 7.2.5+), abstract over
        the client version. Construct a concrete version with
        WDTFogs.for_version(expansion). See https://wowdev.wiki/WDT.)")
  ]] WDTFogsBase
  {
  };

  namespace detail
  {
    // --- version-range trait bases (unwelded) ---------------------------------

    /** The fog volumes, first shipped with content in 8.0.1.25902. */
    struct FogsBfa
    {
      [[
        =chunk("VFOG"),
        =since(builds::BfA_Beta_25902),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("The volumetric fogs (VFOG, 8.0+).")]]
      std::vector<VolumetricFog> fogs;
    };

    /** The version-2 extension records (11.0+). */
    struct FogsTww
    {
      [[
        =chunk("VFEX"),
        =since(builds::TWW_Alpha_54935),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("The VFOG extension records (VFEX, 11.0+, format version 2), "
                     "matched to fogs by id.")]]
      std::vector<VolumetricFogExtra> fog_extras;
    };
  }

  namespace detail
  {
    /** A _fogs.wdt volumetric-fog satellite for one client version (Legion
        7.2.5+). Instantiate through the canonicalizing wdt::fogs::WDTFogs
        alias, never directly.
        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WDT#fogs */
    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc(R"(
          A _fogs.wdt volumetric-fog satellite for one client version (Legion
          7.2.5+): the map's placed fog volumes. Most maps ship it empty. See
          https://wowdev.wiki/WDT.)")
    ]] WDTFogs
      : ChunkedFile<WDTFogs<V>>, WDTFogsBase,
        slot<V, builds::BfA_Beta_25902, FogsBfa>,
        slot<V, builds::TWW_Alpha_54935, FogsTww>
    {
      static constexpr ClientVersion version = V;

      [[
        =chunk("MVER"),
        =welder::doc("The file format version: 1 since Legion 7.2.5, 2 since "
                     "11.0.0.54935 (adds VFEX).")]]
      std::uint32_t mver = V >= builds::TWW_Alpha_54935 ? 2 : 1;

      /** The canonical chunk-stream order the serializer emits a fresh entity
          in (see write_order). Lists every chunk member exactly once. */
      static constexpr std::array chunk_order = {
        four_cc("MVER"), four_cc("VFOG"), four_cc("VFEX"),
      };
    };
  }

  /** A _fogs.wdt satellite — the canonicalizing face of detail::WDTFogs:
      three instantiations (Legion; BfA-Dragonflight; TWW+) serve every
      release. */
  template <ClientVersion V>
    requires(V >= builds::Legion_TombOfSargeras)
  using WDTFogs =
    detail::WDTFogs<canonical_version(V, wdt_fogs_pivots, wdt_fogs_versions)>;
}
