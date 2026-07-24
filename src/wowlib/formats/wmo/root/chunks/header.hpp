#pragma once

/** @file
    WMO root header (MOHD) and its flag bits (namespace wowlib::formats::wmo::root::chunks). */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::root::chunks
{
  // --- MOHD -------------------------------------------------------------------

  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Root header flag bits (SMOHeader.flags).")
  ]] HeaderFlags : std::uint16_t
  {
    do_not_attenuate_vertices [[=welder::doc("Do not attenuate vertices based on portal distance.")]] = 0x1,
    use_unified_render_path [[=welder::doc("Attenuate on portal exit (unified render path).")]] = 0x2,
    use_liquid_type_dbc_id [[=welder::doc("MLIQ ids are LiquidType foreign keys, not the legacy set.")]] = 0x4,
    do_not_fix_vertex_color_alpha [[=welder::doc("Skip the vertex-color alpha fixup (FixColorVertexAlpha).")]] = 0x8,
    lod [[=welder::doc("The WMO has LOD group files (Legion+).")]] = 0x10,
    default_max_lod [[=welder::doc("num_lod is defaulted, implying 3 LOD levels (BfA+).")]] = 0x20
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The MOHD root header: entity counts, ambient color, bounds and "
                 "root-wide flags.")
  ]] SMOHeader
  {
    // Three of the seven counts are DERIVED wire fields: WMORoot::patch_chunk
    // stamps n_groups/n_portals/n_doodad_sets from the owning tables on write
    // and the bindings hide them — the containers are the single source of
    // truth (surveyed across both test clients: every file agrees). The other
    // four counts are NOT derivable — real client files store legacy values
    // their containers do not reproduce — so they stay ordinary stored
    // fields, preserved verbatim for the byte-perfect round-trip.

    [[=welder::doc("Number of textures. NOT derived: hundreds of client files "
                   "store more than the MOTX entry count (exporter-era "
                   "duplicates), and 8.3+ FileDataID-era files keep a legacy "
                   "value with no MOTX at all; preserved as stored.")]]
    std::uint32_t n_textures = 0;

    [[=welder::mark::exclude,
      =welder::doc("Number of group files; derived from the MOGI group-info "
                   "table on write and hidden from bindings.")]]
    std::uint32_t n_groups = 0;

    [[=welder::mark::exclude,
      =welder::doc("Number of portals; derived from the MOPT table on write "
                   "and hidden from bindings.")]]
    std::uint32_t n_portals = 0;

    [[=welder::doc("Number of lights (MOLT). NOT derived: a handful of 8.x "
                   "files store a different count (extra non-MOLT lights); "
                   "preserved as stored.")]]
    std::uint32_t n_lights = 0;

    [[=welder::doc("Number of doodad filenames. Matches the MODN entry count "
                   "pre-8.3, but FileDataID-era files keep a legacy value "
                   "unrelated to MODI's length; preserved as stored.")]]
    std::uint32_t n_doodad_names = 0;

    [[=welder::doc("Number of doodad placements (MODD). NOT derived: many "
                   "client files store an inflated legacy count; preserved "
                   "as stored.")]]
    std::uint32_t n_doodad_defs = 0;

    [[=welder::mark::exclude,
      =welder::doc("Number of doodad sets; derived from the MODS table on "
                   "write and hidden from bindings.")]]
    std::uint32_t n_doodad_sets = 0;

    [[=welder::doc("Base ambient color.")]]
    CArgb ambient_color{};

    [[=welder::doc("Foreign key into WMOAreaTable (m_WMOID).")]]
    std::uint32_t wmo_id = 0;

    [[=welder::doc("Bounding box of the whole object in model space.")]]
    CAaBox bounding_box{};

    [[=welder::doc("Root-wide flags; HeaderFlags bits.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Number of LOD levels including the base (Legion+; zero before).")]]
    std::uint16_t num_lod = 0;
  };
  static_assert(sizeof(SMOHeader) == 0x40);

}
