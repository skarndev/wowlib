#pragma once

/** @file
    The WMO root-file entity (namespace wowlib::formats::wmo::root): header,
    materials, group metadata, portals, lights, doodads, fog and the
    later-expansion volume/light extensions, declared in canonical client chunk
    order. The root wire structs it is built from live in wmo::root::chunks. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/serializer.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>
#include <wowlib/formats/wmo/root/chunks/doodad.hpp>
#include <wowlib/formats/wmo/root/chunks/environment.hpp>
#include <wowlib/formats/wmo/root/chunks/header.hpp>
#include <wowlib/formats/wmo/root/chunks/light.hpp>
#include <wowlib/formats/wmo/root/chunks/material.hpp>
#include <wowlib/formats/wmo/root/chunks/structure.hpp>

namespace wowlib::formats::wmo::root
{
  using namespace wowlib::formats::wmo::root::chunks;

  /** The version-agnostic base of every WMORoot<V> (welded as "WMORoot").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WMORoot* classes a common welded supertype, so binding
      users can write version-agnostic code (`isinstance(x, WMORoot)`, a
      `x: WMORoot` annotation, `WMORoot.for_version(expansion)`). It has no role in
      the C++ API, where you use the concrete WMORoot<V> directly.

      @see https://wowdev.wiki/WMO */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMORoot"),
    =welder::doc(R"(
        A WMO root file, abstract over the client version. The root file lists the
        object's shared data (materials, doodads, portals, lights, fog and the
        group table); the geometry lives in the separate group files. Construct a
        concrete version with WMORoot.for_version(expansion); the per-version
        WMORoot* classes are subclasses. See https://wowdev.wiki/WMO.)")
  ]] WMORootBase
  {
  };

  /** A WMO (world map object) root file for one client version.

      The root file holds everything shared across the object — the material and
      texture tables, doodad (M2) sets and placements, portals, lights, fog, the
      convex-volume/visible-block culling data and the per-group table (MOGI/GFID)
      — while the actual 3D geometry lives in the separate group files
      (WMOGroup). Chunks are declared in canonical client chunk order; an instance
      read from a client file rewrites byte-for-byte until modified.

      @tparam V the client version this layout targets.
      @see https://wowdev.wiki/WMO */
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A WMO root file for one client version. The root lists the object's shared
        data — materials and textures, doodad (M2) sets and placements, portals,
        lights, fog, culling volumes and the per-group table — while the 3D
        geometry lives in the separate group files (see WMOGroup). Chunks are laid
        out in canonical client order; an instance read from a client file
        rewrites byte-for-byte until modified. See https://wowdev.wiki/WMO.)")
  ]] WMORoot : ChunkedFile<WMORoot<V>>, WMORootBase
  {
    static constexpr ClientVersion version = V;

    [[
      =chunk("MVER"),
      =welder::doc("The WMO format version; 17 for every supported client.")]]
    std::uint32_t mver = wmo_version_v17;

    [[
      =chunk("MOHD"),
      =welder::doc("The root header (MOHD).")]]
    SMOHeader header{};

    [[
      =chunk("MOTX"),
      =formats::optional,
      =welder::doc(R"(Texture filenames (MOTX; pre-8.1, or the 8.1+ fallback mode —
                      its presence means MOMT texture fields are offsets into it,
                      not FileDataIDs).)")]]
    StringBlock textures;

    [[
      =chunk("MOMT"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Materials (MOMT).")]]
    std::vector<SMOMaterial> materials;

    [[
      =chunk("MOM3"),
      =since(ClientVersion{11, 0, 0, 54210}),
      =formats::optional,
      =welder::doc(R"(M3 materials (MOM3, 11.0+); when present, MOMT is ignored. An
                      m3SI blob, kept opaque.)")]]
    ChunkBlob materials_m3;

    [[
      =chunk("MOUV"),
      =since(ClientVersion{7, 3, 0, 24473}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Texture-coordinate translation animations (MOUV, 7.3+), one
                      per material.)")]]
    std::vector<UVAnimation> uv_animations;

    [[
      =chunk("MOGN"),
      =formats::optional,
      =welder::doc("Group names (MOGN), referenced by byte offset.")]]
    StringBlock group_names;

    [[
      =chunk("MOGI"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Per-group info (MOGI).")]]
    std::vector<SMOGroupInfo> group_infos;

    [[
      =chunk("MOSB"),
      =formats::optional,
      =welder::doc(R"(Skybox filename (MOSB); raw bytes — files pad it to 4-byte
                      alignment.)")]]
    ChunkBlob skybox_name;

    [[
      =chunk("MOSI"),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Skybox FileDataID (MOSI, 8.1+).")]]
    std::vector<std::uint32_t> skybox_fdid;

    [[
      =chunk("MOPV"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Portal vertices (MOPV).")]]
    std::vector<C3Vector> portal_vertices;

    [[
      =chunk("MOPT"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Portals (MOPT).")]]
    std::vector<SMOPortal> portals;

    [[
      =chunk("MOPR"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Portal references from groups (MOPR).")]]
    std::vector<SMOPortalRef> portal_refs;

    [[
      =chunk("MOPE"),
      =since(ClientVersion{11, 1, 0, 58221}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Portal extra data (MOPE, 11.1+).")]]
    std::vector<PortalExtra> portal_extras;

    [[
      =chunk("MOVV"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Visible block vertices (MOVV).")]]
    std::vector<C3Vector> visible_block_vertices;

    [[
      =chunk("MOVB"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Visible blocks (MOVB).")]]
    std::vector<SMOVisibleBlock> visible_blocks;

    [[
      =chunk("MOLT"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Lights (MOLT).")]]
    std::vector<SMOLight> lights;

    [[
      =chunk("MOLV"),
      =since(ClientVersion{9, 1, 0, 39015}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Directional-gradient light extensions (MOLV, 9.1+); entries
                      reference lights by index.)")]]
    std::vector<LightExtension> light_extensions;

    [[
      =chunk("MODS"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Doodad sets (MODS).")]]
    std::vector<SMODoodadSet> doodad_sets;

    [[
      =chunk("MODN"),
      =formats::optional,
      =welder::doc(R"(Doodad (M2) filenames (MODN; pre-8.1 or fallback, see
                      textures).)")]]
    StringBlock doodad_names;

    [[
      =chunk("MODI"),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Doodad FileDataIDs (MODI, 8.1+; replaces doodad_names).")]]
    std::vector<std::uint32_t> doodad_fdids;

    [[
      =chunk("MODD"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Doodad placements (MODD).")]]
    std::vector<SMODoodadDef> doodad_defs;

    [[
      =chunk("MFOG"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Fog volumes (MFOG).")]]
    std::vector<SMOFog> fogs;

    [[
      =chunk("MCVP"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Convex volume planes (MCVP); transports mostly.")]]
    std::vector<C4Plane> convex_volume_planes;

    [[
      =chunk("GFID"),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Group file FileDataIDs (GFID, Legion+), in group order;
                      repeated per LOD level for LOD WMOs.)")]]
    std::vector<std::uint32_t> group_fdids;

    [[
      =chunk("MDDI"),
      =since(ClientVersion{8, 3, 0, 32044}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Per-doodad color multipliers (MDDI, 8.3+), applied to the
                      MODD color.)")]]
    std::vector<float> doodad_color_mults;

    [[
      =chunk("MPVD"),
      =since(ClientVersion{8, 3, 0, 32044}),
      =formats::optional,
      =welder::doc(R"(Particulate volume data (MPVD, 8.3+); undocumented layout,
                      kept opaque.)")]]
    ChunkBlob particulate_volumes;

    [[
      =chunk("MAVG"),
      =since(ClientVersion{8, 3, 0, 32044}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Global ambient volumes (MAVG, 8.3+); position and radii are
                      zero, selected by doodad set.)")]]
    std::vector<AmbientVolume> global_ambient_volumes;

    [[
      =chunk("MAVD"),
      =since(ClientVersion{8, 3, 0, 32044}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Ambient volumes (MAVD, 8.3+), overriding the header ambient
                      color inside their radius.)")]]
    std::vector<AmbientVolume> ambient_volumes;

    [[
      =chunk("MBVD"),
      =since(ClientVersion{8, 3, 0, 32044}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Box ambient volumes (MBVD, 8.3+); read only when MAVG/MAVD is
                      present.)")]]
    std::vector<AmbientBoxVolume> ambient_box_volumes;

    [[
      =chunk("MFED"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Fog extra data (MFED, 9.0+); same count as MFOG.")]]
    std::vector<FogExtra> fog_extras;

    [[
      =chunk("MGI2"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Group info v2 (MGI2, 9.0+); same count as MOGI, overrides LOD
                      selection.)")]]
    std::vector<GroupInfo2> group_infos2;

    [[
      =chunk("MNLD"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Dynamic lights (MNLD, 9.0+): torch fires, window light
                      projection and the like.)")]]
    std::vector<NewLight> new_lights;

    [[
      =chunk("MDDL"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::doc(R"(Detail (ground-effect) doodad layers (MDDL, 9.0+);
                      variable-length RLE layout, kept opaque.)")]]
    ChunkBlob detail_doodads;

    [[
      =chunk("MOMX"),
      =formats::optional,
      =welder::doc("MOMX (11.x, undocumented purpose); preserved opaque.")]]
    ChunkBlob momx;

    // MFOB (12.1+, Midnight) postdates the supported client range; it and any
    // other unmodeled root chunk round-trip through ChunkExtras::unknown.
  };
}
