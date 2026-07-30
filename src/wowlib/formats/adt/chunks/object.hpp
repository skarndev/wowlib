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
    =welder::weld(welder::lang::py, welder::lang::lua),
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

  /** One Shadowlands doodad-set range (MWDR): a [begin, end] span into MWDS. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
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
