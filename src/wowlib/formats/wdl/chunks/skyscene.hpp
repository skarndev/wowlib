#pragma once

/** @file
    WDL sky-scene chunk binary structs (namespace wowlib::formats::wdl::chunks),
    Shadowlands+: distant scripted scenery — sky scenes tied to
    SkySceneXPlayerCondition, their conditions and placed objects, plus the
    War Within scene-living schedule records. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wdl::chunks
{
  struct [[
    =welder::weld,
    =welder::doc("One sky scene (an MSSN record, Shadowlands+), tying a "
                 "SkySceneID to its condition and object ranges.")
  ]] SkyScene
  {
    [[=welder::doc("Foreign key into SkySceneXPlayerCondition (SkySceneID).")]]
    std::uint32_t sky_scene_id = 0;

    [[=welder::doc("Unknown; possibly the SkySceneXPlayerCondition record count.")]]
    std::uint32_t unk_1 = 0;

    [[=welder::doc("First condition record in MSSC.")]]
    std::int16_t condition_index = 0;

    [[=welder::doc("Condition record count.")]]
    std::int16_t condition_count = 0;

    [[=welder::doc("First object record in MSSO.")]]
    std::uint16_t object_index = 0;

    [[=welder::doc("Object record count.")]]
    std::int16_t object_count = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_4 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_5 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_6 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_7 = 0;
  };
  static_assert(sizeof(SkyScene) == 32);

  struct [[
    =welder::weld,
    =welder::doc("One sky-scene condition (an MSSC record, Shadowlands+).")
  ]] SkySceneCondition
  {
    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_0 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_1 = 0;

    [[=welder::doc("What condition_value keys into: 1 AreaTable (a parent of the "
                   "player's area), 2 LightParams, 3 LightSkybox (the current "
                   "skybox), 5 ZoneLight.")]]
    std::uint32_t condition_type = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_3 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_4 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_5 = 0;

    [[=welder::doc("The db2 key condition_type selects.")]]
    std::uint32_t condition_value = 0;
  };
  static_assert(sizeof(SkySceneCondition) == 28);

  struct [[
    =welder::weld,
    =welder::doc("One sky-scene object (an MSSO record, Shadowlands+): a placed "
                 "distant M2.")
  ]] SkySceneObject
  {
    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_0 = 0;

    [[=welder::doc("Flags; 0x1 means the object has an MSSF params record.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("The model FileDataID.")]]
    std::uint32_t fdid = 0;

    [[=welder::doc("Translation.")]]
    C3Vector translation{};

    [[=welder::doc("Rotation as Euler angles, radians.")]]
    C3Vector rotation{};

    [[=welder::doc("Uniform scale.")]]
    float scale = 1;

    [[=welder::doc("Index into MSSF when flags has 0x1.")]]
    std::uint32_t params_index = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_11 = 0;
  };
  static_assert(sizeof(SkySceneObject) == 48);

  struct [[
    =welder::weld,
    =welder::doc("One sky-scene object params record (an MSSF record; documented "
                 "as Dragonflight+ but observed in 9.2.7 files).")
  ]] SkySceneObjectParams
  {
    [[=welder::doc("Unknown.")]]
    float unk_0 = 0;

    [[=welder::doc("Unknown.")]]
    float unk_1 = 0;
  };
  static_assert(sizeof(SkySceneObjectParams) == 8);

  struct [[
    =welder::weld,
    =welder::doc("One scene-living (world) definition (an MSLD record, The War "
                 "Within+): a time-windowed scene schedule.")
  ]] SceneLivingDef
  {
    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_0 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_1 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_2 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk_3 = 0;

    [[=welder::doc("Window start (first pair value).")]]
    std::uint32_t time_start_0 = 0;

    [[=welder::doc("Window start (second pair value).")]]
    std::uint32_t time_start_1 = 0;

    [[=welder::doc("Window end (first pair value).")]]
    std::uint32_t time_end_0 = 0;

    [[=welder::doc("Window end (second pair value).")]]
    std::uint32_t time_end_1 = 0;
  };
  static_assert(sizeof(SceneLivingDef) == 32);
}
