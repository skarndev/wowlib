#pragma once

/** @file
    The WMO group-file entities (namespace wowlib::formats::wmo::group): the
    MOGP container body (geometry, batches, collision, liquid and the
    later-expansion light/volume references) and the group file wrapping it,
    declared in canonical client chunk order. The group wire structs it is built
    from live in wmo::group_chunks. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/serializer.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>
#include <wowlib/formats/wmo/group_chunks/geometry.hpp>
#include <wowlib/formats/wmo/group_chunks/header.hpp>
#include <wowlib/formats/wmo/group_chunks/light.hpp>

namespace wowlib::formats::wmo::group
{
  using namespace wowlib::formats::wmo::group_chunks;

  /** The version-agnostic base of every WMOGroupBody<V> (welded as
      "WMOGroupBody"): the facade's abstract MOGP-payload entity. Empty. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMOGroupBody"),
    =welder::doc("The MOGP container payload, abstract over the client version. "
                 "Construct a concrete version with "
                 "WMOGroupBody.for_version(expansion).")
  ]] WMOGroupBodyBase
  {
  };

  /** The version-agnostic base of every WMOGroup<V> (welded as "WMOGroup"): the
      facade's abstract group-file entity. Empty. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMOGroup"),
    =welder::doc("One WMO group file, abstract over the client version. "
                 "Construct a concrete version with "
                 "WMOGroup.for_version(expansion); the per-version WMOGroup* "
                 "classes are subclasses.")
  ]] WMOGroupBase
  {
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The MOGP payload for one client version: the group header and "
                 "the geometry subchunks (triangles, vertices, normals, batches, "
                 "BSP, liquid, lights).")
  ]] WMOGroupBody : ChunkedFile<WMOGroupBody<V>>, WMOGroupBodyBase
  {
    static constexpr ClientVersion version = V;

    [[=formats::header, =welder::doc("The group header leading the MOGP payload.")]]
    SMOGroupHeader<V> header{};

    [[=chunk("MOGX"), =since(wmo_query_faces), =formats::optional,
      =welder::doc("Query face start (MOGX, 10.0+): the base subtracted from a "
                   "polygon index into MOQG. A single value in practice."), =welder::mark::no_reassign]]
    std::vector<std::uint32_t> query_face_start;

    [[=chunk("MOPY"), =formats::optional,
      =welder::doc("Per-triangle material info (MOPY)."), =welder::mark::no_reassign]]
    std::vector<SMOPoly> polys;

    [[=chunk("MPY2"), =since(wmo_query_faces), =formats::optional,
      =welder::doc("Per-triangle material info v2 (MPY2, 10.0+; replaces MOPY)."), =welder::mark::no_reassign]]
    std::vector<Poly2> polys2;

    [[=chunk("MOVI"), =formats::optional,
      =welder::doc("Triangle vertex indices (MOVI), three per triangle."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> indices;

    [[=chunk("MOVX"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("32-bit triangle vertex indices (MOVX, ~9.0+; the large-mesh "
                   "MOVI replacement)."), =welder::mark::no_reassign]]
    std::vector<std::uint32_t> large_indices;

    [[=chunk("MOVT"), =formats::optional, =welder::doc("Vertices (MOVT)."), =welder::mark::no_reassign]]
    std::vector<C3Vector> vertices;

    [[=chunk("MONR"), =formats::optional, =welder::doc("Normals (MONR)."), =welder::mark::no_reassign]]
    std::vector<C3Vector> normals;

    /** Up to three texture-coordinate sets; count driven by the group flags
        (has_two_motv, has_three_motv). Excluded from bindings until Repeated<>
        grows a sequence protocol. */
    [[=chunk("MOTV"), =formats::optional, =repeats(3), =welder::mark::exclude]]
    Repeated<std::vector<C2Vector>, 3> texcoords;

    [[=chunk("MOBA"), =formats::optional, =welder::doc("Render batches (MOBA)."), =welder::mark::no_reassign]]
    std::vector<SMOBatch<V>> batches;

    [[=chunk("MOQG"), =since(wmo_query_faces), =formats::optional,
      =welder::doc("Per-polygon ground types (MOQG, 10.0+), indexed by polygon "
                   "index minus the MOGX base."), =welder::mark::no_reassign]]
    std::vector<std::uint32_t> query_faces;

    [[=chunk("MOLR"), =formats::optional,
      =welder::doc("Light references into the root's MOLT (MOLR)."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> light_refs;

    [[=chunk("MODR"), =formats::optional,
      =welder::doc("Doodad references into the root's MODD (MODR)."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> doodad_refs;

    [[=chunk("MOBN"), =formats::optional,
      =welder::doc("Collision BSP nodes (MOBN)."), =welder::mark::no_reassign]]
    std::vector<CAaBspNode> bsp_nodes;

    [[=chunk("MOBR"), =formats::optional,
      =welder::doc("BSP face indices (MOBR)."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> bsp_face_indices;

    /** Up to two vertex-color layers (has_vertex_colors, has_two_mocv).
        Excluded from bindings like texcoords. */
    [[=chunk("MOCV"), =formats::optional, =repeats(2), =welder::mark::exclude]]
    Repeated<std::vector<CImVector>, 2> vertex_colors;

    [[=chunk("MOC2"), =formats::optional,
      =welder::doc("Second vertex-color-like weights (MOC2), used by the "
                   "parallax and shader-23 materials."), =welder::mark::no_reassign]]
    std::vector<CImVector> vertex_colors2;

    [[=chunk("MLIQ"), =formats::optional,
      =welder::doc("Liquid data (MLIQ); intra-chunk offset structure, kept "
                   "opaque for now.")]]
    ChunkBlob liquid;

    [[=chunk("MORI"), =formats::optional,
      =welder::doc("Triangle-strip indices (MORI)."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> trans_batch_indices;

    [[=chunk("MORB"),
      =since(wmo_trans_batch_data),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Triangle-strip batch overrides (MORB, Cata+); same count "
                   "as MOBA.")]]
    std::vector<RenderBatchOverride> batch_overrides;

    [[=chunk("MOTA"), =since(wmo_trans_batch_data), =formats::optional,
      =welder::doc("Tangent arrays (MOTA, Cata+); offset-based layout, kept "
                   "opaque.")]]
    ChunkBlob tangents;

    [[=chunk("MOBS"), =since(wmo_trans_batch_data), =formats::optional,
      =welder::doc("Shadow batches (MOBS, Cata+)."), =welder::mark::no_reassign]]
    std::vector<ShadowBatch> shadow_batches;

    [[=chunk("MDAL"), =since(wmo_ambient_override), =formats::optional,
      =welder::doc("Ambient color override (MDAL, WoD+); a single color in "
                   "practice, replacing the header ambient."), =welder::mark::no_reassign]]
    std::vector<CArgb> ambient_color_override;

    [[=chunk("MOPL"), =since(wmo_ambient_override), =formats::optional,
      =welder::doc("Terrain-cutting planes (MOPL, WoD+); requires the "
                   "can_cut_terrain flag, at most 32."), =welder::mark::no_reassign]]
    std::vector<C4Plane> terrain_cut_planes;

    [[=chunk("MOPB"), =since(wmo_legion), =formats::optional,
      =welder::doc("Prepass batches (MOPB, Legion+); undocumented 24-byte "
                   "records, kept opaque.")]]
    ChunkBlob prepass_batches;

    [[=chunk("MOLS"), =since(wmo_legion), =formats::optional,
      =welder::doc("Spot lights (MOLS, Legion+); undocumented 56-byte records, "
                   "kept opaque.")]]
    ChunkBlob spot_lights;

    [[=chunk("MOLP"), =since(wmo_legion), =formats::optional,
      =welder::doc("Point lights (MOLP, Legion+)."), =welder::mark::no_reassign]]
    std::vector<PointLight> point_lights;

    [[=chunk("MLSS"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Spot-light sets (MLSS, 8.1+): (first, count) ranges into "
                   "MOLS per doodad set."), =welder::mark::no_reassign]]
    std::vector<LightSet> spot_light_sets;

    [[=chunk("MLSP"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Point-light sets (MLSP, 8.1+): (first, count) ranges into "
                   "MOLP per doodad set."), =welder::mark::no_reassign]]
    std::vector<LightSet> point_light_sets;

    [[=chunk("MLSK"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Point-light animation sets (MLSK, 8.1+): (first, count) "
                   "ranges into MOP2."), =welder::mark::no_reassign]]
    std::vector<LightSet> point_light_anim_sets;

    [[=chunk("MOP2"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Animated point lights (MOP2, 8.1+)."), =welder::mark::no_reassign]]
    std::vector<PointLightAnim> point_light_anims;

    [[=chunk("MPVR"), =since(wmo_volumes), =formats::optional,
      =welder::doc("Particulate volume references (MPVR, 8.3+) into the root's "
                   "MPVD."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> particulate_refs;

    [[=chunk("MAVR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Ambient volume references (MAVR, 9.0+) into the root's "
                   "MAVD."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> ambient_volume_refs;

    [[=chunk("MBVR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Box volume references (MBVR, 9.0+) into the root's MBVD."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> box_volume_refs;

    [[=chunk("MFVR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Fog volume references (MFVR, 9.0+) into the root's MFOG "
                   "and MFED."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> fog_volume_refs;

    [[=chunk("MNLR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("New-light references (MNLR, 9.0+) into the root's MNLD."), =welder::mark::no_reassign]]
    std::vector<std::uint16_t> new_light_refs;

    // v14-alpha-only subchunks (MOLM/MOLD, MOIN, lightmap MOLV, MPB*) predate
    // the supported range; anything else unmodeled stays in
    // ChunkExtras::unknown, round-tripping verbatim.
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One WMO group file for one client version: the format version "
                 "and the MOGP container holding the geometry.")
  ]] WMOGroup : ChunkedFile<WMOGroup<V>>, WMOGroupBase
  {
    static constexpr ClientVersion version = V;

    [[=chunk("MVER"),
      =welder::doc("The WMO format version; 17 for every supported client.")]]
    std::uint32_t mver = wmo_version_v17;

    [[=chunk("MOGP"), =formats::container,
      =welder::doc("The MOGP container: group header and geometry.")]]
    WMOGroupBody<V> body{};
  };
}
