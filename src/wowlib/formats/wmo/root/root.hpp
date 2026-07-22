#pragma once

/** @file
    The WMO root-file entity (namespace wowlib::formats::wmo::root): header,
    materials, group metadata, portals, lights, doodads, fog and the
    later-expansion volume/light extensions, declared in canonical client chunk
    order. The root wire structs it is built from live in wmo::chunks. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/serializer.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>
#include <wowlib/formats/wmo/chunks/doodad.hpp>
#include <wowlib/formats/wmo/chunks/environment.hpp>
#include <wowlib/formats/wmo/chunks/header.hpp>
#include <wowlib/formats/wmo/chunks/light.hpp>
#include <wowlib/formats/wmo/chunks/material.hpp>
#include <wowlib/formats/wmo/chunks/structure.hpp>

namespace wowlib::formats::wmo::root
{
  using namespace wowlib::formats::wmo::chunks;

  /** The version-agnostic base of every WMORoot<V> (welded as "WMORoot"): the
      facade's abstract root-file entity and for_version() host. Empty; its
      per-version subclasses carry the chunks. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMORoot"),
    =welder::doc("A WMO root file, abstract over the client version. Construct a "
                 "concrete version with WMORoot.for_version(expansion); the "
                 "per-version WMORoot* classes are subclasses.")
  ]] WMORootBase
  {
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A WMO root file for one client version: header, materials, group
        metadata, portals, lights, doodad sets and placements, fog and the
        later-expansion extensions. An instance read from a client file rewrites
        byte-for-byte until modified.)")
  ]] WMORoot : ChunkedFile<WMORoot<V>>, WMORootBase
  {
    static constexpr ClientVersion version = V;

    [[=chunk("MVER"),
      =welder::doc("The WMO format version; 17 for every supported client.")]]
    std::uint32_t mver = wmo_version_v17;

    [[=chunk("MOHD"), =welder::doc("The root header (MOHD).")]]
    SMOHeader header{};

    [[=chunk("MOTX"), =formats::optional,
      =welder::doc("Texture filenames (MOTX; pre-8.1, or the 8.1+ fallback mode - "
                   "its presence means MOMT texture fields are offsets into it, "
                   "not FileDataIDs).")]]
    StringBlock textures;

    [[=chunk("MOMT"), =formats::optional, =welder::doc("Materials (MOMT)."), =welder::mark::no_reassign]]
    std::vector<SMOMaterial> materials;

    [[=chunk("MOM3"), =since(wmo_m3_materials), =formats::optional,
      =welder::doc("M3 materials (MOM3, 11.0+); when present, MOMT is ignored. "
                   "An m3SI blob, kept opaque.")]]
    ChunkBlob materials_m3;

    [[=chunk("MOUV"), =since(wmo_uv_animation), =formats::optional,
      =welder::doc("Texture-coordinate translation animations (MOUV, 7.3+), one "
                   "per material."), =welder::mark::no_reassign]]
    std::vector<UVAnimation> uv_animations;

    [[=chunk("MOGN"), =formats::optional,
      =welder::doc("Group names (MOGN), referenced by byte offset.")]]
    StringBlock group_names;

    [[=chunk("MOGI"), =formats::optional, =welder::doc("Per-group info (MOGI)."), =welder::mark::no_reassign]]
    std::vector<SMOGroupInfo> group_infos;

    [[=chunk("MOSB"), =formats::optional,
      =welder::doc("Skybox filename (MOSB); raw bytes - files pad it to 4-byte "
                   "alignment.")]]
    ChunkBlob skybox_name;

    [[=chunk("MOSI"), =since(wmo_fdid_refs), =formats::optional,
      =welder::doc("Skybox FileDataID (MOSI, 8.1+)."), =welder::mark::no_reassign]]
    std::vector<std::uint32_t> skybox_fdid;

    [[=chunk("MOPV"), =formats::optional, =welder::doc("Portal vertices (MOPV)."), =welder::mark::no_reassign]]
    std::vector<C3Vector> portal_vertices;

    [[=chunk("MOPT"), =formats::optional, =welder::doc("Portals (MOPT)."), =welder::mark::no_reassign]]
    std::vector<SMOPortal> portals;

    [[=chunk("MOPR"), =formats::optional,
      =welder::doc("Portal references from groups (MOPR)."), =welder::mark::no_reassign]]
    std::vector<SMOPortalRef> portal_refs;

    [[=chunk("MOPE"), =since(wmo_portal_extras), =formats::optional,
      =welder::doc("Portal extra data (MOPE, 11.1+)."), =welder::mark::no_reassign]]
    std::vector<PortalExtra> portal_extras;

    [[=chunk("MOVV"), =formats::optional,
      =welder::doc("Visible block vertices (MOVV)."), =welder::mark::no_reassign]]
    std::vector<C3Vector> visible_block_vertices;

    [[=chunk("MOVB"), =formats::optional, =welder::doc("Visible blocks (MOVB)."), =welder::mark::no_reassign]]
    std::vector<SMOVisibleBlock> visible_blocks;

    [[=chunk("MOLT"), =formats::optional, =welder::doc("Lights (MOLT)."), =welder::mark::no_reassign]]
    std::vector<SMOLight> lights;

    [[=chunk("MOLV"), =since(wmo_light_extensions), =formats::optional,
      =welder::doc("Directional-gradient light extensions (MOLV, 9.1+); "
                   "entries reference lights by index."), =welder::mark::no_reassign]]
    std::vector<LightExtension> light_extensions;

    [[=chunk("MODS"), =formats::optional, =welder::doc("Doodad sets (MODS)."), =welder::mark::no_reassign]]
    std::vector<SMODoodadSet> doodad_sets;

    [[=chunk("MODN"), =formats::optional,
      =welder::doc("Doodad (M2) filenames (MODN; pre-8.1 or fallback, see "
                   "textures).")]]
    StringBlock doodad_names;

    [[=chunk("MODI"), =since(wmo_fdid_refs), =formats::optional,
      =welder::doc("Doodad FileDataIDs (MODI, 8.1+; replaces doodad_names)."), =welder::mark::no_reassign]]
    std::vector<std::uint32_t> doodad_fdids;

    [[=chunk("MODD"), =formats::optional,
      =welder::doc("Doodad placements (MODD)."), =welder::mark::no_reassign]]
    std::vector<SMODoodadDef> doodad_defs;

    [[=chunk("MFOG"), =formats::optional, =welder::doc("Fog volumes (MFOG)."), =welder::mark::no_reassign]]
    std::vector<SMOFog> fogs;

    [[=chunk("MCVP"), =formats::optional,
      =welder::doc("Convex volume planes (MCVP); transports mostly."), =welder::mark::no_reassign]]
    std::vector<C4Plane> convex_volume_planes;

    [[=chunk("GFID"), =since(wmo_legion), =formats::optional,
      =welder::doc("Group file FileDataIDs (GFID, Legion+), in group order; "
                   "repeated per LOD level for LOD WMOs."), =welder::mark::no_reassign]]
    std::vector<std::uint32_t> group_fdids;

    [[=chunk("MDDI"), =since(wmo_volumes), =formats::optional,
      =welder::doc("Per-doodad color multipliers (MDDI, 8.3+), applied to the "
                   "MODD color."), =welder::mark::no_reassign]]
    std::vector<float> doodad_color_mults;

    [[=chunk("MPVD"), =since(wmo_volumes), =formats::optional,
      =welder::doc("Particulate volume data (MPVD, 8.3+); undocumented layout, "
                   "kept opaque.")]]
    ChunkBlob particulate_volumes;

    [[=chunk("MAVG"), =since(wmo_volumes), =formats::optional,
      =welder::doc("Global ambient volumes (MAVG, 8.3+); position and radii are "
                   "zero, selected by doodad set."), =welder::mark::no_reassign]]
    std::vector<AmbientVolume> global_ambient_volumes;

    [[=chunk("MAVD"), =since(wmo_volumes), =formats::optional,
      =welder::doc("Ambient volumes (MAVD, 8.3+), overriding the header ambient "
                   "color inside their radius."), =welder::mark::no_reassign]]
    std::vector<AmbientVolume> ambient_volumes;

    [[=chunk("MBVD"), =since(wmo_volumes), =formats::optional,
      =welder::doc("Box ambient volumes (MBVD, 8.3+); read only when MAVG/MAVD "
                   "is present."), =welder::mark::no_reassign]]
    std::vector<AmbientBoxVolume> ambient_box_volumes;

    [[=chunk("MFED"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Fog extra data (MFED, 9.0+); same count as MFOG."), =welder::mark::no_reassign]]
    std::vector<FogExtra> fog_extras;

    [[=chunk("MGI2"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Group info v2 (MGI2, 9.0+); same count as MOGI, overrides "
                   "LOD selection."), =welder::mark::no_reassign]]
    std::vector<GroupInfo2> group_infos2;

    [[=chunk("MNLD"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Dynamic lights (MNLD, 9.0+): torch fires, window light "
                   "projection and the like."), =welder::mark::no_reassign]]
    std::vector<NewLight> new_lights;

    [[=chunk("MDDL"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Detail (ground-effect) doodad layers (MDDL, 9.0+); "
                   "variable-length RLE layout, kept opaque.")]]
    ChunkBlob detail_doodads;

    [[=chunk("MOMX"), =formats::optional,
      =welder::doc("MOMX (11.x, undocumented purpose); preserved opaque.")]]
    ChunkBlob momx;

    // MFOB (12.1+, Midnight) postdates the supported client range; it and any
    // other unmodeled root chunk round-trip through ChunkExtras::unknown.
  };
}
