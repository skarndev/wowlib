#pragma once

/** @file
    The map-placement binary records SHARED across the world file formats
    (namespace wowlib::formats::common, like the math primitives): SMMapObjDef
    places a WMO on a map (WDT MODF, WDL MODF, ADT MODF) and SMDoodadDef places
    an M2 (ADT MDDF, WDL MLDD), under their canonical client names. Both are
    trivially copyable with exact on-disk layout. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::common
{
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("WMO placement flag bits (SMMapObjDef.flags).")
  ]] MapObjDefFlags : std::uint16_t
  {
    destroyable [[=welder::doc("A destroyable, server-controllable building (e.g. the "
                               "DeathknightStart tower).")]] = 0x1,
    use_lod [[=welder::doc("Also load the _LOD1 WMO, selected by distance (WoD+).")]] = 0x2,
    has_scale [[=welder::doc("The scale field is engaged: scale / 1024 applies "
                             "(Legion+; otherwise 1.0).")]] = 0x4,
    entry_is_fdid [[=welder::doc("name_id is a FileDataID to load directly, not a name-table "
                                 "index (Legion+).")]] = 0x8,
    use_sets_from_mwds [[=welder::doc("Doodad set indices come from the ADT MWDS chunk "
                                      "(Shadowlands+).")]] = 0x80
  };

  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("M2 placement flag bits (SMDoodadDef.flags).")
  ]] DoodadDefFlags : std::uint16_t
  {
    biodome [[=welder::doc("Sets the internal WDOODADDEF 0x800 flag; meaning unknown "
                           "(biodome).")]] = 0x1,
    shrubbery [[=welder::doc("Shrubbery; not checked by 6.0.1+ clients.")]] = 0x2,
    unk_4 [[=welder::doc("Unknown (Legion+).")]] = 0x4,
    unk_8 [[=welder::doc("Unknown (Legion+).")]] = 0x8,
    unk_10 [[=welder::doc("Unknown (Shadowlands+); sets flag 0x4 on the PVS doodad.")]] = 0x10,
    liquid_known [[=welder::doc("SMDoodadDef::Flag_liquidKnown (Legion+).")]] = 0x20,
    entry_is_fdid [[=welder::doc("name_id is a FileDataID to load directly, not an MMID "
                                 "index (Legion+).")]] = 0x40,
    unk_100 [[=welder::doc("Unknown (Legion+).")]] = 0x100,
    accept_proj_textures [[=welder::doc("Accepts projected textures (Legion+).")]] = 0x1000
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A WMO placement (the 64-byte MODF record of WDT, WDL and ADT): which
        object, where, and how it is instanced.)")
  ]] SMMapObjDef
  {
    [[=welder::doc(R"(The object: an MWID/MWMO name-table reference, or a FileDataID
                      when flags has entry_is_fdid (Legion+). The WDT global-WMO
                      record ignores it and uses the MWMO content.)")]]
    std::uint32_t name_id = 0;

    [[=welder::doc("Unique instance id across the whole map (WDT global-WMO records "
                   "leave it unused).")]]
    std::uint32_t unique_id = 0;

    [[=welder::doc("Position, in the map's placement coordinate system.")]]
    C3Vector position{};

    [[=welder::doc("Rotation as Euler angles, degrees.")]]
    C3Vector rotation{};

    [[=welder::doc("The transformed object's bounding box (position plus the rotated "
                   "extents); culling and collision use it.")]]
    CAaBox extents{};

    [[=welder::doc("Flags; MapObjDefFlags bits.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("The WMO doodad set shown by this instance (MODS index, or MWDR when "
                   "use_sets_from_mwds is set).")]]
    std::uint16_t doodad_set = 0;

    [[=welder::doc("The WMO name set (renames the same model per instance).")]]
    std::uint16_t name_set = 0;

    [[=welder::doc("Scale, 1024 = 1.0, engaged by the has_scale flag (Legion+); padding "
                   "in older clients.")]]
    std::uint16_t scale = 0;
  };
  static_assert(sizeof(SMMapObjDef) == 0x40);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        An M2 (doodad) placement (the 36-byte MDDF record of ADT, reused by the
        WDL MLDD low-resolution set): which model, where, and at what scale.)")
  ]] SMDoodadDef
  {
    [[=welder::doc("The model: an MMID name-table reference, or a FileDataID when flags "
                   "has entry_is_fdid (Legion+; WDL MLDD entries are always "
                   "FileDataIDs).")]]
    std::uint32_t name_id = 0;

    [[=welder::doc("Unique instance id across the whole map.")]]
    std::uint32_t unique_id = 0;

    [[=welder::doc("Position, in the map's placement coordinate system.")]]
    C3Vector position{};

    [[=welder::doc("Rotation as Euler angles, degrees.")]]
    C3Vector rotation{};

    [[=welder::doc("Scale, 1024 = 1.0.")]]
    std::uint16_t scale = 1024;

    [[=welder::doc("Flags; DoodadDefFlags bits.")]]
    std::uint16_t flags = 0;
  };
  static_assert(sizeof(SMDoodadDef) == 0x24);
}
