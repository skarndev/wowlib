#pragma once

/** @file
    WDT main-file chunk binary structs (namespace wowlib::formats::wdt::root::chunks):
    the MPHD map header (both the pre-8.1 layout and the 8.1+ FileDataID
    layout), the MAIN tile table entry and the MAID per-tile FileDataID
    record, with their flag enums. */

#include <array>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>

namespace wowlib::formats::wdt::root::chunks {
  // --- MPHD -------------------------------------------------------------------

  enum class [[
      =welder::weld,
      =welder::doc("Map header flag bits (SMMapHeader.flags).")
    ]] MapHeaderFlags : std::uint32_t {
    uses_global_map_obj [[=welder::doc(
      "A WMO-only map: one global map object (MWMO/MODF), "
      "no terrain tiles.")]] = 0x1,
    adt_has_mccv [[=welder::doc(
      "ADTs carry MCCV vertex colors (WotLK+); with this set "
      "every ADT of the map must have MCCV.")]] = 0x2,
    adt_has_big_alpha [[=welder::doc(
      "Terrain shader 2 (_env): 4096-byte uncompressed MCAL "
      "alpha maps instead of 2048.")]] = 0x4,
    adt_has_doodadrefs_sorted_by_size_cat
    [[=welder::doc("ADT MCRF/MCRD doodad references are sorted by size "
      "category.")]] = 0x8,
    adt_has_mclv [[=welder::doc(
      "ADTs carry the second MCLV color layer (Cata+; deprecated "
      "and forbidden in 8.x).")]] = 0x10,
    adt_has_upside_down_ground [[=welder::doc(
      "Flip the ground upside down to create a "
      "ceiling (Cata+).")]] = 0x20,
    unk_0x40 [[=
      welder::doc("Unknown (MoP+); seen on Firelands2.wdt before Legion.")]] =
    0x40,
    adt_has_height_texturing [[=welder::doc(
      "Terrain shader 6: alpha maps are influenced by "
      "the _h textures and MTXP (MoP+); also 4096-byte "
      "uncompressed MCAL entries.")]] = 0x80,
    load_lod_adt [[=
      welder::doc("Load _lod.adt (Legion+); implicitly sets 0x8000.")]] = 0x100,
    has_maid [[=welder::doc(
      "The WDT carries MAID: ADTs load by FileDataID instead of "
      "path (8.1.0.28294+).")]] = 0x200,
    unk_0x400 [[=welder::doc("Unknown.")]] = 0x400,
    unk_0x800 [[=welder::doc("Unknown.")]] = 0x800,
    unk_0x1000 [[=welder::doc("Unknown.")]] = 0x1000,
    unk_0x2000 [[=welder::doc("Unknown.")]] = 0x2000,
    unk_0x4000 [[=welder::doc("Unknown.")]] = 0x4000,
    unk_0x8000 [[=welder::doc(
      "Affects _lod.adt rendering; implicitly set for the continent "
      "maps (Legion+). Without it (or 0x100) distant map textures "
      "and LOD do not render.")]] = 0x8000
  };

  /** The version-agnostic base of every SMMapHeader<V>, welded as
      "WDTHeader".

      This base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version header classes a common welded supertype so
      binding users can write version-agnostic code (isinstance,
      WDTHeader.for_version(expansion)). It has no role in the C++ API, where
      you use the concrete SMMapHeader<V> directly. It is an empty (elided)
      base, so the binary specializations stay trivially copyable and 32 bytes. */
  struct [[
      =welder::weld,
      =welder::weld_as("WDTHeader"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc("An MPHD map header, abstract over the client version. "
        "Construct a concrete version with "
        "WDTHeader.for_version(expansion).")
    ]] WDTHeaderBase {};

  /** The 32-byte MPHD map header. Pre-8.1 only the flags (and a legacy second
      integer) are read; 8.1.0.28294 repurposes the unused tail into the
      satellite-file FileDataIDs. */
  namespace detail {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct SMMapHeader;

    template <ClientVersion V> requires(V < wdt_map_fdids)
    struct [[
      =welder::weld,
      =welder::doc("The MPHD map header: the map-wide flags; the rest of the "
        "record is unused before 8.1.")
    ]] WOWLIB_EMPTY_BASES SMMapHeader<V> : WDTHeaderBase{
      [[=welder::doc("Map-wide flags; MapHeaderFlags bits.")]]
      std::uint32_t flags = 0;

      [[=welder::doc(
        "A legacy value the WotLK-era client reads but does not use "
        "meaningfully; zero in shipped files.")]]
      std::uint32_t something = 0;

      [[=welder::doc("Unused padding.")]]
      std::array<std::uint32_t, 6> unused{};



    };

    template <ClientVersion V> requires(V >= wdt_map_fdids)
    struct [[
      =welder::weld,
      =welder::doc(
        "The MPHD map header: the map-wide flags and the satellite-file "
        "FileDataIDs (8.1+).")
    ]] WOWLIB_EMPTY_BASES SMMapHeader<V> : WDTHeaderBase{
      [[=welder::doc("Map-wide flags; MapHeaderFlags bits.")]]
      std::uint32_t flags = 0;

      [[=welder::doc("FileDataID of the _lgt.wdt lights satellite, or 0.")]]
      std::uint32_t lgt_fdid = 0;

      [[=welder::doc("FileDataID of the _occ.wdt occlusion satellite, or 0.")]]
      std::uint32_t occ_fdid = 0;

      [[=welder::doc(
        "FileDataID of the _fogs.wdt volumetric-fog satellite, or 0.")]]
      std::uint32_t fogs_fdid = 0;

      [[=welder::doc(
        "FileDataID of the _mpv.wdt particulate-volume satellite, or 0.")]]
      std::uint32_t mpv_fdid = 0;

      [[=welder::doc("FileDataID of the _tex.wdt texture-list satellite, or 0.")
      ]]
      std::uint32_t tex_fdid = 0;

      [[=welder::doc(
        "FileDataID of the map's .wdl low-resolution heightmap, or 0.")]]
      std::uint32_t wdl_fdid = 0;

      [[=welder::doc("FileDataID of the map's .pd4 file, or 0.")]]
      std::uint32_t pd4_fdid = 0;



    };
  }

  /** The MPHD header — the canonicalizing face of detail::SMMapHeader
      (wdt_header_pivots: the FileDataID relayout at 8.1.0.28294), so two
      instantiations cover all eleven releases. */
  template <ClientVersion V>
  using SMMapHeader = detail::SMMapHeader<canonical_version(V, wdt_header_pivots, wdt_versions)>;

  static_assert(sizeof(SMMapHeader<versions::wotlk>) == 32);
  static_assert(sizeof(SMMapHeader<versions::shadowlands>) == 32);

  // --- MAIN -------------------------------------------------------------------

  enum class [[
      =welder::weld,
      =welder::doc("Tile table flag bits (SMAreaInfo.flags).")
    ]] AreaInfoFlags : std::uint32_t {
    has_adt [[=welder::doc("The tile has an ADT.")]] = 0x1,
    all_water [[=welder::doc(
      "The whole tile is \"fake water\" (Cataclysm+; pre-Cata "
      "clients read this bit as their loaded flag).")]] = 0x2,
    loaded [[=welder::doc("Runtime-only loaded marker; zero in files.")]] = 0x4
  };

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MAIN tile table entry (SMAreaInfo). The table holds 64 x 64 of
        them in row-major order (y outer, x inner).)")
    ]] SMAreaInfo {
    [[=welder::doc("Tile flags; AreaInfoFlags bits.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Runtime-only async load id; zero in files.")]]
    std::uint32_t async_id = 0;
  };

  static_assert(sizeof(SMAreaInfo) == 8);

  // --- MAID -------------------------------------------------------------------

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MAID per-tile FileDataID record (8.1.0.28294+): every file
        belonging to one map tile. The table holds 64 x 64 of them in
        row-major order (y outer, x inner); absent files are 0.)")
    ]] MapFileDataIDs {
    [[=welder::doc("FileDataID of mapname_xx_yy.adt (the root ADT).")]]
    std::uint32_t root_adt = 0;

    [[=welder::doc("FileDataID of mapname_xx_yy_obj0.adt.")]]
    std::uint32_t obj0_adt = 0;

    [[=welder::doc("FileDataID of mapname_xx_yy_obj1.adt.")]]
    std::uint32_t obj1_adt = 0;

    [[=welder::doc("FileDataID of mapname_xx_yy_tex0.adt.")]]
    std::uint32_t tex0_adt = 0;

    [[=welder::doc("FileDataID of mapname_xx_yy_lod.adt.")]]
    std::uint32_t lod_adt = 0;

    [[=welder::doc("FileDataID of the mapname_xx_yy.blp map texture.")]]
    std::uint32_t map_texture = 0;

    [[=welder::doc("FileDataID of the mapname_xx_yy_n.blp normal map texture.")]
    ]
    std::uint32_t map_texture_n = 0;

    [[=welder::doc("FileDataID of the mapxx_yy.blp minimap texture.")]]
    std::uint32_t minimap_texture = 0;
  };

  static_assert(sizeof(MapFileDataIDs) == 32);
}
