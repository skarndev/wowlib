#pragma once

/** @file
    The MOGP group header and group flag bits (namespace wowlib::formats::wmo::group::chunks). */

#include <array>
#include <cstdint>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>

namespace wowlib::formats::wmo::group::chunks {
  // --- MOGP header ------------------------------------------------------------

  enum class [[
      =welder::weld,
      =welder::doc("Group flag bits (MOGP and MOGI flags).")
    ]] GroupFlags : std::uint32_t {
    HasBsp [[=welder::doc("Has a collision BSP tree (MOBN/MOBR).")]] = 0x1,
    HasLightMap [[=
      welder::doc("Has a light map (v14 legacy; unused since vanilla).")]] =
    0x2,
    HasVertexColors [[=welder::doc("Has vertex colors (MOCV).")]] = 0x4,
    Exterior [[=welder::doc("Outdoor group (SMOGroup::EXTERIOR).")]] = 0x8,
    ExteriorLit [[=welder::doc("Use exterior lighting; no local diffuse.")]] = 0x40,
    Unreachable [[=welder::doc("Unreachable (SMOGroup::UNREACHABLE).")]] = 0x80,
    ShowExteriorSky [[=
      welder::doc("Show the exterior sky inside this interior group.")]] =
    0x100,
    HasLights [[=welder::doc("Has light references (MOLR).")]] = 0x200,
    Lod [[=
      welder::doc(
        "Legion+: an LOD group. Pre-Cata: marked the unused MPB* chunks.")]] =
    0x400,
    HasDoodads [[=welder::doc("Has doodad references (MODR).")]] = 0x800,
    HasLiquid [[=
      welder::doc("Has liquid (MLIQ), or an implicitly full volume.")]] =
    0x1000,
    Interior [[=welder::doc("Indoor group (SMOGroup::INTERIOR).")]] = 0x2000,
    QueryMountAllowed [[=
      welder::doc("Pre-WotLK: whether mount queries are allowed.")]] = 0x8000,
    AlwaysDraw [[=
      welder::doc("Always draw (clears exterior after group creation).")]] =
    0x10000,
    HasTriangleStrips [[=welder::doc("Has triangle-strip data (MORI/MORB).")]] = 0x20000,
    ShowSkybox [[=
      welder::doc("Show the skybox; unset automatically when MOSB is absent.")]]
    = 0x40000,
    Ocean [[=
      welder::doc("The liquid is ocean, not water (LiquidType handling).")]] =
    0x80000,
    MountAllowed [[=welder::doc("Mounting is allowed in this group.")]] = 0x200000,
    HasTwoMocv [[=
      welder::doc("Has a second MOCV layer; only its alpha is used.")]] =
    0x1000000,
    HasTwoMotv [[=
      welder::doc("Has two MOTV texcoord sets (SMOGroup::TVERTS2).")]] =
    0x2000000,
    Antiportal [[=
      welder::doc("An antiportal occluder group (SMOGroup::ANTIPORTAL).")]] =
    0x4000000,
    ExteriorCull [[=
      welder::doc("Exterior culling (SMOGroup::EXTERIOR_CULL, Legion+).")]] =
    0x20000000,
    HasThreeMotv [[=welder::doc(
      "Has three MOTV texcoord sets, e.g. for shader 18.")]] = 0x40000000
  };

  enum class [[
      =welder::weld,
      =welder::doc("Extended group flag bits (MOGP.flags2 and MGI2).")
    ]] GroupFlags2 : std::uint32_t {
    CanCutTerrain [[=
      welder::doc("Has terrain-cutting portal planes (MOPL, WoD+).")]] = 0x1,
    IsSplitGroupParent [[=welder::doc("Parent of a split group (9.2+).")]] = 0x40,
    IsSplitGroupChild [[=welder::doc("Child of a split group (9.2+).")]] = 0x80,
    AttachmentMesh [[=welder::doc("An attachment mesh (9.2 - 11.0).")]] = 0x100
  };

  /** The version-agnostic base of every SMOGroupHeader<V>, welded as
      "WMOGroupHeader".

      This base exists ENTIRELY for the language bindings (Python, Lua): it gives
      the per-version WMOGroupHeader* classes a common welded supertype so binding
      users can write version-agnostic code (isinstance,
      WMOGroupHeader.for_version(expansion)). It has no role in the C++ API, where
      you use the concrete SMOGroupHeader<V> directly. It is an empty (elided)
      base, so the binary specializations stay trivially copyable and 0x44 bytes. */
  struct [[
      =welder::weld,
      =welder::weld_as("WMOGroupHeader"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc("A MOGP group header, abstract over the client version. "
        "Construct a concrete version with "
        "WMOGroupHeader.for_version(expansion).")
    ]] WMOGroupHeaderBase {};

  /** The 0x44-byte header leading the MOGP container payload. The final u32
      slot is unused up to 9.1.5 and becomes the split-group indices after. */
  namespace detail {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct SMOGroupHeader;

    template <ClientVersion V> requires(V < WmoSplitGroups)
    struct [[
      =welder::weld,
      =welder::doc(
        "The MOGP group header: names, flags, bounds, portal and batch "
        "ranges, fog and liquid.")
    ]] WOWLIB_EMPTY_BASES SMOGroupHeader<V> : WMOGroupHeaderBase{
      [[=welder::doc("Byte offset of the group name in MOGN.")]]
      std::uint32_t groupName = 0;

      [[=welder::doc("Byte offset of the descriptive name in MOGN.")]]
      std::uint32_t descriptiveGroupName = 0;

      [[=welder::doc("Group flags; GroupFlags bits.")]]
      std::uint32_t flags = 0;

      [[=welder::doc("Group bounding box.")]]
      CAaBox boundingBox{};

      [[=welder::doc("First portal reference in MOPR.")]]
      std::uint16_t portalStart = 0;

      [[=welder::doc("Portal reference count.")]]
      std::uint16_t portalCount = 0;

      [[=welder::doc("Transition batch count (MOBA prefix).")]]
      std::uint16_t transBatchCount = 0;

      [[=welder::doc("Interior batch count.")]]
      std::uint16_t intBatchCount = 0;

      [[=welder::doc("Exterior batch count.")]]
      std::uint16_t extBatchCount = 0;

      [[=welder::doc("The fourth batch-count slot; unused by clients.")]]
      std::uint16_t batchTypeD = 0;

      [[=welder::doc("Fog indices into MFOG.")]]
      std::array<std::uint8_t, 4> fogIds{};

      [[=welder::doc("Group liquid type; interpretation depends on the root "
        "use_liquid_type_dbc_id flag.")]]
      std::uint32_t groupLiquid = 0;

      [[=welder::doc("Foreign key into WMOAreaTable (m_WMOGroupID).")]]
      std::uint32_t uniqueId = 0;

      [[=welder::doc("Extended flags (Cataclysm+); GroupFlags2 bits.")]]
      std::uint32_t flags2 = 0;

      [[=welder::doc(
        "Unused up to 9.1.5 (becomes the split-group indices in 9.2+).")]]
      std::uint32_t unused = 0;



    };

    template <ClientVersion V> requires(V >= WmoSplitGroups)
    struct [[
      =welder::weld,
      =welder::doc(
        "The MOGP group header: names, flags, bounds, portal and batch "
        "ranges, fog, liquid and split-group links (9.2+).")
    ]] WOWLIB_EMPTY_BASES SMOGroupHeader<V> : WMOGroupHeaderBase{
      [[=welder::doc("Byte offset of the group name in MOGN.")]]
      std::uint32_t groupName = 0;

      [[=welder::doc("Byte offset of the descriptive name in MOGN.")]]
      std::uint32_t descriptiveGroupName = 0;

      [[=welder::doc("Group flags; GroupFlags bits.")]]
      std::uint32_t flags = 0;

      [[=welder::doc("Group bounding box.")]]
      CAaBox boundingBox{};

      [[=welder::doc("First portal reference in MOPR.")]]
      std::uint16_t portalStart = 0;

      [[=welder::doc("Portal reference count.")]]
      std::uint16_t portalCount = 0;

      [[=welder::doc("Transition batch count (MOBA prefix).")]]
      std::uint16_t transBatchCount = 0;

      [[=welder::doc("Interior batch count.")]]
      std::uint16_t intBatchCount = 0;

      [[=welder::doc("Exterior batch count.")]]
      std::uint16_t extBatchCount = 0;

      [[=welder::doc("The fourth batch-count slot; unused by clients.")]]
      std::uint16_t batchTypeD = 0;

      [[=welder::doc("Fog indices into MFOG.")]]
      std::array<std::uint8_t, 4> fogIds{};

      [[=welder::doc("Group liquid type; interpretation depends on the root "
        "use_liquid_type_dbc_id flag.")]]
      std::uint32_t groupLiquid = 0;

      [[=welder::doc("Foreign key into WMOAreaTable (m_WMOGroupID).")]]
      std::uint32_t uniqueId = 0;

      [[=welder::doc("Extended flags (Cataclysm+); GroupFlags2 bits.")]]
      std::uint32_t flags2 = 0;

      [[=welder::doc(
        "Parent split group, or the first child (9.2+ split groups).")]]
      std::int16_t parentOrFirstChildSplitGroupIndex = -1;

      [[=welder::doc("Next sibling in the split-group chain.")]]
      std::int16_t nextSplitChildGroupIndex = -1;



    };
  }

  /** The MOGP header — the canonicalizing face of detail::SMOGroupHeader
      (WmoGroupHeaderPivots: the split-group indices arrive at 9.2), so
      two instantiations cover all eleven releases. */
  template <ClientVersion V>
  using SMOGroupHeader = detail::SMOGroupHeader<canonicalVersion(V, WmoGroupHeaderPivots, WmoVersions)>;


  static_assert(sizeof(SMOGroupHeader<versions::Wotlk>) == 0x44);
  static_assert(sizeof(SMOGroupHeader<versions::Shadowlands>) == 0x44);
}
