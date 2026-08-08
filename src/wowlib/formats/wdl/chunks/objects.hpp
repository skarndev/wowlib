#pragma once

/** @file
    WDL object-placement chunk binary structs (namespace
    wowlib::formats::wdl::chunks). The placements themselves reuse the shared
    map records (SMMapObjDef for MODF/MLMD-style entries, SMDoodadDef for
    MLDD; formats/common/map_placements.hpp) — this header adds the
    Legion-era LOD records the WDL shares with the ADT _obj1 split file. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/map_placements.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wdl::chunks
{
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A low-resolution WMO placement (an MLMD record, Legion+): an
        SMMapObjDef without the extents — those live in the parallel MLMX
        record. name_id is always a FileDataID here (the WDL has no name
        table).)")
  ]] LodMapObjDef
  {
    [[=welder::doc("The WMO FileDataID.")]]
    std::uint32_t name_id = 0;

    [[=welder::doc("Unique instance id across the whole map; matches the full-"
                   "resolution ADT placement.")]]
    std::uint32_t unique_id = 0;

    [[=welder::doc("Position, in the map's placement coordinate system.")]]
    C3Vector position{};

    [[=welder::doc("Rotation as Euler angles, degrees.")]]
    C3Vector rotation{};

    [[=welder::doc("Flags; MapObjDefFlags bits.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("The WMO doodad set shown by this instance.")]]
    std::uint16_t doodad_set = 0;

    [[=welder::doc("The WMO name set.")]]
    std::uint16_t name_set = 0;

    [[=welder::doc("Scale, 1024 = 1.0.")]]
    std::uint16_t scale = 0;
  };
  static_assert(sizeof(LodMapObjDef) == 0x28);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        Visibility extents for one low-resolution placement (an MLDX/MLMX
        record, Legion+): the transformed model's bounding box and a radius,
        in server coordinates. Same count and order as the placement list it
        accompanies.)")
  ]] LodExtent
  {
    [[=welder::doc("The transformed model's bounding box.")]]
    CAaBox bounds{};

    [[=welder::doc("The visibility radius.")]]
    float radius = 0;
  };
  static_assert(sizeof(LodExtent) == 28);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A fade-distance range for one low-resolution M2 placement (an MLDF
        record, BfA): undocumented on wowdev; layout established by surveying
        every 8.3.7 WDL carrying the chunk (33 files) — always exactly one
        8-byte record per MLDD entry, holding a pair of ascending distances
        drawn from clean round values ((500, 3500), (3500, 4000),
        (5000, 5050), (10000, 10050)).)")
  ]] LodDoodadFade
  {
    [[=welder::doc("The distance the fade starts at.")]]
    float fade_start = 0;

    [[=welder::doc("The distance the fade ends at.")]]
    float fade_end = 0;
  };
  static_assert(sizeof(LodDoodadFade) == 8);
}
