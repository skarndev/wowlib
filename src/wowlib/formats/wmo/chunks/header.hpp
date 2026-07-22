#pragma once

/** @file
    WMO root header (MOHD) and its flag bits (namespace wowlib::formats::wmo::chunks). */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::chunks
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
    [[=welder::doc("Number of textures referenced by the materials.")]]
    std::uint32_t n_textures = 0;

    [[=welder::doc("Number of group files belonging to this WMO.")]]
    std::uint32_t n_groups = 0;

    [[=welder::doc("Number of portals (MOPT).")]]
    std::uint32_t n_portals = 0;

    [[=welder::doc("Number of lights (MOLT).")]]
    std::uint32_t n_lights = 0;

    [[=welder::doc("Number of doodad filenames (MODN/MODI).")]]
    std::uint32_t n_doodad_names = 0;

    [[=welder::doc("Number of doodad placements (MODD).")]]
    std::uint32_t n_doodad_defs = 0;

    [[=welder::doc("Number of doodad sets (MODS).")]]
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
