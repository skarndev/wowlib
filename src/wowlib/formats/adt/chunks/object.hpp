#pragma once

/** @file
    ADT object/misc binary structs (namespace wowlib::formats::adt::chunks): the
    per-cell sound emitter (MCSE) and the Shadowlands doodad-set range (MWDR).
    Doodad and WMO placements (MDDF/MODF) reuse the shared SMDoodadDef/SMMapObjDef
    from common/map_placements.hpp; blend-mesh and LOD records live in lod.hpp. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::adt::chunks
{
  /** One sound emitter (MCSE, 28 bytes): the client reads 0x1C bytes per entry
      — a sound-entry id and a position/size pair. */
  struct [[
    =welder::weld,
    =welder::doc(R"(
        A terrain sound emitter (MCSE, 28 bytes): a SoundEntriesAdvanced foreign
        key and a position/size pair placing an ambient sound in the cell.)")
  ]] CWSoundEmitter
  {
    [[=welder::doc("The sound entry (a SoundEntriesAdvanced foreign key).")]]
    std::uint32_t entry_id = 0;

    [[=welder::doc("The emitter position.")]]
    C3Vector position{};

    [[=welder::doc("The emitter size / extents.")]]
    C3Vector size{};

    bool operator==(const CWSoundEmitter&) const = default;
  };
  static_assert(sizeof(CWSoundEmitter) == 0x1C);

  /** One pre-WotLK sound emitter (MCSE, 52 bytes): the full inline emitter
      parameters later folded into SoundEntriesAdvanced. Field layout per
      wowdev's 1.12.1 struct, verified field-for-field against the 1.12.2
      client's seven carrier tiles (positions land inside their tiles' world
      ranges, times are day-minutes, gaps are milliseconds). 2.4.3 ships no
      MCSE at all, so the layout is vanilla-only in practice. */
  struct [[
    =welder::weld,
    =welder::doc(R"(
        A pre-WotLK terrain sound emitter (MCSE, 52 bytes): a SoundEntries
        foreign key plus the inline distance/time/loop parameters that later
        moved into SoundEntriesAdvanced.)")
  ]] CWSoundEmitterVanilla
  {
    [[=welder::doc("Sequential emitter id, unique per map.")]]
    std::uint32_t sound_point_id = 0;

    [[=welder::doc("The sound (a SoundEntries foreign key).")]]
    std::uint32_t sound_name_id = 0;

    [[=welder::doc("The emitter position, in world coordinates.")]]
    C3Vector position{};

    [[=welder::doc("Full-volume distance.")]]
    float min_distance = 0;

    [[=welder::doc("Audible-range distance.")]]
    float max_distance = 0;

    [[=welder::doc("Cutoff distance.")]]
    float cutoff_distance = 0;

    [[=welder::doc("Daily start time, minutes.")]]
    std::uint16_t start_time = 0;

    [[=welder::doc("Daily end time, minutes.")]]
    std::uint16_t end_time = 0;

    [[=welder::doc("Emitter mode.")]]
    std::uint16_t mode = 0;

    [[=welder::doc("Minimum loop count.")]]
    std::uint8_t loop_count_min = 0;

    [[=welder::doc("Maximum loop count.")]]
    std::uint8_t loop_count_max = 0;

    [[=welder::doc("Minimum group silence, milliseconds.")]]
    std::uint16_t group_silence_min = 0;

    [[=welder::doc("Maximum group silence, milliseconds.")]]
    std::uint16_t group_silence_max = 0;

    [[=welder::doc("Minimum concurrent play instances.")]]
    std::uint16_t play_instances_min = 0;

    [[=welder::doc("Maximum concurrent play instances.")]]
    std::uint16_t play_instances_max = 0;

    [[=welder::doc("Minimum gap between sounds, milliseconds.")]]
    std::uint16_t inter_sound_gap_min = 0;

    [[=welder::doc("Maximum gap between sounds, milliseconds.")]]
    std::uint16_t inter_sound_gap_max = 0;

    bool operator==(const CWSoundEmitterVanilla&) const = default;
  };
  static_assert(sizeof(CWSoundEmitterVanilla) == 52);

  /** One Shadowlands doodad-set range (MWDR): a [begin, end] span into MWDS. */
  struct [[
    =welder::weld,
    =welder::doc(R"(
        A doodad-set range (MWDR, Shadowlands+): an inclusive [begin, end] span
        into the MWDS index list, selecting which WMO doodad sets a placement
        loads.)")
  ]] SMDoodadSetRange
  {
    [[=welder::doc("The first MWDS index (inclusive).")]]
    std::uint32_t begin = 0;
    [[=welder::doc("The last MWDS index (inclusive).")]]
    std::uint32_t end = 0;

    bool operator==(const SMDoodadSetRange&) const = default;
  };
  static_assert(sizeof(SMDoodadSetRange) == 0x8);
}
