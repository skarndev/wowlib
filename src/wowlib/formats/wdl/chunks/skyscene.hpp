#pragma once

/** @file
    WDL sky-scene chunk binary structs (namespace wowlib::formats::wdl::chunks),
    Shadowlands+: distant scripted scenery — sky scenes tied to
    SkySceneXPlayerCondition, their conditions and placed objects, plus the
    War Within scene-living schedule records. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wdl::chunks {
  struct [[
      =welder::weld,
      =welder::doc("One sky scene (an MSSN record, Shadowlands+), tying a "
        "SkySceneID to its condition and object ranges.")
    ]] SkyScene {
    [[=welder::doc("Foreign key into SkySceneXPlayerCondition (SkySceneID).")]]
    std::uint32_t skySceneId = 0;

    [[=welder::doc(
      "Unknown; possibly the SkySceneXPlayerCondition record count.")]]
    std::uint32_t unk1 = 0;

    [[=welder::doc("First condition record in MSSC.")]]
    std::int16_t conditionIndex = 0;

    [[=welder::doc("Condition record count.")]]
    std::int16_t conditionCount = 0;

    [[=welder::doc("First object record in MSSO.")]]
    std::uint16_t objectIndex = 0;

    [[=welder::doc("Object record count.")]]
    std::int16_t objectCount = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk4 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk5 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk6 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk7 = 0;
  };

  static_assert(sizeof(SkyScene) == 32);

  struct [[
      =welder::weld,
      =welder::doc("One sky-scene condition (an MSSC record, Shadowlands+).")
    ]] SkySceneCondition {
    [[=welder::doc("Unknown.")]]
    std::uint32_t unk0 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk1 = 0;

    [[=welder::doc(
      "What condition_value keys into: 1 AreaTable (a parent of the "
      "player's area), 2 LightParams, 3 LightSkybox (the current "
      "skybox), 5 ZoneLight.")]]
    std::uint32_t conditionType = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk3 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk4 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk5 = 0;

    [[=welder::doc("The db2 key condition_type selects.")]]
    std::uint32_t conditionValue = 0;
  };

  static_assert(sizeof(SkySceneCondition) == 28);

  struct [[
      =welder::weld,
      =welder::doc(
        "One sky-scene object (an MSSO record, Shadowlands+): a placed "
        "distant M2.")
    ]] SkySceneObject {
    [[=welder::doc("Unknown.")]]
    std::uint32_t unk0 = 0;

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
    std::uint32_t paramsIndex = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk11 = 0;
  };

  static_assert(sizeof(SkySceneObject) == 48);

  struct [[
      =welder::weld,
      =welder::doc(
        "One sky-scene object params record (an MSSF record; documented "
        "as Dragonflight+ but observed in 9.2.7 files).")
    ]] SkySceneObjectParams {
    [[=welder::doc("Unknown.")]]
    float unk0 = 0;

    [[=welder::doc("Unknown.")]]
    float unk1 = 0;
  };

  static_assert(sizeof(SkySceneObjectParams) == 8);

  struct [[
      =welder::weld,
      =welder::doc(
        "One scene-living (world) definition (an MSLD record, The War "
        "Within+): a time-windowed scene schedule.")
    ]] SceneLivingDef {
    [[=welder::doc("Unknown.")]]
    std::uint32_t unk0 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk1 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk2 = 0;

    [[=welder::doc("Unknown.")]]
    std::uint32_t unk3 = 0;

    [[=welder::doc("Window start (first pair value).")]]
    std::uint32_t timeStart0 = 0;

    [[=welder::doc("Window start (second pair value).")]]
    std::uint32_t timeStart1 = 0;

    [[=welder::doc("Window end (first pair value).")]]
    std::uint32_t timeEnd0 = 0;

    [[=welder::doc("Window end (second pair value).")]]
    std::uint32_t timeEnd1 = 0;
  };

  static_assert(sizeof(SceneLivingDef) == 32);
}
