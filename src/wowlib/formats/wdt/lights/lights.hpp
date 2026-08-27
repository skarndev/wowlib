#pragma once

/** @file
    The _lgt.wdt lights satellite entity (namespace wowlib::formats::wdt::lights),
    WoD+: freely placed map lights — point lights under lamp posts and the
    like, plus Legion's spot lights and light texture animations. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>
#include <wowlib/formats/wdt/lights/chunks/records.hpp>

namespace wowlib::formats::wdt::lights {
  using namespace wowlib::formats::wdt::lights::chunks;

  /** The version-agnostic base of every WDTLights<V> (welded as
      "WDTLights"); bindings-only, like every *Base.
      @see https://wowdev.wiki/WDT#lgt */
  struct [[
      =welder::weld,
      =welder::weld_as("WDTLights"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A _lgt.wdt lights satellite (WoD+), abstract over the client version:
        the map's freely placed point and spot lights. Construct a concrete
        version with WDTLights.for_version(expansion). See
        https://wowdev.wiki/WDT.)")
    ]] WDTLightsBase {};

  namespace detail {
    // --- version-range trait bases (unwelded) ---------------------------------

    /** The WoD-only point lights, dropped when Legion introduces MPL2. The
        since() matches the satellite file's own introduction, so the docs
        badge reads "WoD", not "Vanilla-WoD" — no _lgt.wdt predates WoD. */
    struct LightsWod {
      [[
        =chunk("MPLT"),
        =since(builds::WoD),
        =until(builds::Legion),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          "The WoD point lights (MPLT); Legion+ clients ignore them in "
          "favor of MPL2.")]]
      std::vector<MapPointLightLegacy> legacyPointLights;
    };

    /** The Legion light set (point v2, spot, textures, animations). */
    struct LightsLegion {
      [[
        =chunk("MPL2"),
        =since(builds::Legion),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("The Legion point lights (MPL2); 9.0+ files carry MPL3 "
          "instead.")]]
      std::vector<MapPointLight> pointLights;

      [[
        =chunk("MSLT"),
        =since(builds::Legion),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("The spot lights (MSLT, Legion+).")]]
      std::vector<MapSpotLight> spotLights;

      [[
        =chunk("MTEX"),
        =since(builds::Legion),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          "Light-cookie texture FileDataIDs (MTEX, Legion+), referenced "
          "by texture_index.")]]
      std::vector<std::uint32_t> textureFdids;

      [[
        =chunk("MLTA"),
        =since(builds::Legion),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Light texture animations (MLTA, Legion+), referenced by "
          "mlta_index.")]]
      std::vector<LightAnimation> lightAnimations;
    };

    /** The Shadowlands point-light revision. */
    struct LightsSL {
      [[
        =chunk("MPL3"),
        =since(builds::SL_Beta_34490),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          "The Shadowlands point lights (MPL3, 9.0.1.34490+; replaces "
          "MPL2 in shipped files).")]]
      std::vector<MapPointLight3> pointLightsV3;
    };
  }

  namespace detail {
    /** A _lgt.wdt lights satellite for one client version (WoD+): the map's
        freely placed lights. WMO-only maps ship the file with just its MVER.
        Instantiate through the canonicalizing wdt::lights::WDTLights alias,
        never directly.
        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WDT#lgt */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          A _lgt.wdt lights satellite for one client version (WoD+): freely
          placed point lights (MPLT/MPL2/MPL3 by era), spot lights, their
          textures and animations. See https://wowdev.wiki/WDT.)")
      ]] WDTLights
      : ChunkedFile<WDTLights<V>>,
        WDTLightsBase,
        Slot<V, builds::WoD, LightsWod, builds::Legion>,
        Slot<V, builds::Legion, LightsLegion>,
        Slot<V, builds::SL_Beta_34490, LightsSL> {
      static constexpr ClientVersion Version = V;

      [[
        =chunk("MVER"),
        =welder::doc("The file format version: 18 up to Legion 7.0.1.20740, 20 "
          "since 7.0.1.20914.")]]
      std::uint32_t mver = V >= builds::Legion_Alpha_20914 ? 20 : WdtVersion18;

      /** The canonical chunk-stream order the serializer emits a fresh entity
          in (see writeOrder). Lists every chunk member exactly once. */
      static constexpr std::array ChunkOrder = {
        fourCc("MVER"),
        fourCc("MPLT"),
        fourCc("MPL2"),
        fourCc("MPL3"),
        fourCc("MSLT"),
        fourCc("MTEX"),
        fourCc("MLTA"),
      };
    };
  }

  /** A _lgt.wdt satellite — the canonicalizing face of detail::WDTLights:
      three instantiations (WoD; Legion-BfA; Shadowlands+) serve every
      release. */
  template <ClientVersion V> requires(V >= builds::WoD)
  using WDTLights = detail::WDTLights<canonicalVersion(V, WdtLightsPivots, WdtSatelliteVersions)>;
}
