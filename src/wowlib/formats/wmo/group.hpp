#pragma once

/** @file
    The WMO group-file entities: the MOGP container body (geometry, batches,
    collision, liquid and the later-expansion light/volume references) and the
    group file wrapping it, declared in canonical client chunk order. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/serializer.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/data_structs.hpp>
#include <wowlib/formats/wmo/root.hpp>

namespace wowlib::formats::wmo
{
  /** The payload of the MOGP container chunk: the group header prelude
      followed by the geometry subchunks. */
  template <ClientVersion V>
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The MOGP payload: the group header and the geometry subchunks "
                 "(triangles, vertices, normals, batches, BSP, liquid, lights).")
  ]]
  WMOGroupBody : ChunkedFile<WMOGroupBody<V>>
  {
    static constexpr ClientVersion version = V;

    [[=formats::header, =welder::doc("The group header leading the MOGP payload.")]]
    SMOGroupHeader<V> header{};

    [[=chunk("MOGX"), =since(wmo_query_faces), =formats::optional,
      =welder::doc("Query face start (MOGX, 10.0+): the base subtracted from a "
                   "polygon index into MOQG. A single value in practice.")]]
    std::vector<std::uint32_t> query_face_start;

    [[=chunk("MOPY"), =formats::optional,
      =welder::doc("Per-triangle material info (MOPY).")]]
    std::vector<SMOPoly> polys;

    [[=chunk("MPY2"), =since(wmo_query_faces), =formats::optional,
      =welder::doc("Per-triangle material info v2 (MPY2, 10.0+; replaces MOPY).")]]
    std::vector<Poly2> polys2;

    [[=chunk("MOVI"), =formats::optional,
      =welder::doc("Triangle vertex indices (MOVI), three per triangle.")]]
    std::vector<std::uint16_t> indices;

    [[=chunk("MOVX"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("32-bit triangle vertex indices (MOVX, ~9.0+; the large-mesh "
                   "MOVI replacement).")]]
    std::vector<std::uint32_t> large_indices;

    [[=chunk("MOVT"), =formats::optional, =welder::doc("Vertices (MOVT).")]]
    std::vector<C3Vector> vertices;

    [[=chunk("MONR"), =formats::optional, =welder::doc("Normals (MONR).")]]
    std::vector<C3Vector> normals;

    /** Up to three texture-coordinate sets; count driven by the group flags
        (has_two_motv, has_three_motv). Excluded from bindings until Repeated<>
        grows a sequence protocol. */
    [[=chunk("MOTV"), =formats::optional, =repeats(3), =welder::mark::exclude]]
    Repeated<std::vector<C2Vector>, 3> texcoords;

    [[=chunk("MOBA"), =formats::optional, =welder::doc("Render batches (MOBA).")]]
    std::vector<SMOBatch<V>> batches;

    [[=chunk("MOQG"), =since(wmo_query_faces), =formats::optional,
      =welder::doc("Per-polygon ground types (MOQG, 10.0+), indexed by polygon "
                   "index minus the MOGX base.")]]
    std::vector<std::uint32_t> query_faces;

    [[=chunk("MOLR"), =formats::optional,
      =welder::doc("Light references into the root's MOLT (MOLR).")]]
    std::vector<std::uint16_t> light_refs;

    [[=chunk("MODR"), =formats::optional,
      =welder::doc("Doodad references into the root's MODD (MODR).")]]
    std::vector<std::uint16_t> doodad_refs;

    [[=chunk("MOBN"), =formats::optional,
      =welder::doc("Collision BSP nodes (MOBN).")]]
    std::vector<CAaBspNode> bsp_nodes;

    [[=chunk("MOBR"), =formats::optional,
      =welder::doc("BSP face indices (MOBR).")]]
    std::vector<std::uint16_t> bsp_face_indices;

    /** Up to two vertex-color layers (has_vertex_colors, has_two_mocv).
        Excluded from bindings like texcoords. */
    [[=chunk("MOCV"), =formats::optional, =repeats(2), =welder::mark::exclude]]
    Repeated<std::vector<CImVector>, 2> vertex_colors;

    [[=chunk("MOC2"), =formats::optional,
      =welder::doc("Second vertex-color-like weights (MOC2), used by the "
                   "parallax and shader-23 materials.")]]
    std::vector<CImVector> vertex_colors2;

    [[=chunk("MLIQ"), =formats::optional,
      =welder::doc("Liquid data (MLIQ); intra-chunk offset structure, kept "
                   "opaque for now.")]]
    ChunkBlob liquid;

    [[=chunk("MORI"), =formats::optional,
      =welder::doc("Triangle-strip indices (MORI).")]]
    std::vector<std::uint16_t> trans_batch_indices;

    [[=chunk("MORB"), =since(wmo_trans_batch_data), =formats::optional,
      =welder::doc("Triangle-strip batch overrides (MORB, Cata+); same count "
                   "as MOBA.")]]
    std::vector<RenderBatchOverride> batch_overrides;

    [[=chunk("MOTA"), =since(wmo_trans_batch_data), =formats::optional,
      =welder::doc("Tangent arrays (MOTA, Cata+); offset-based layout, kept "
                   "opaque.")]]
    ChunkBlob tangents;

    [[=chunk("MOBS"), =since(wmo_trans_batch_data), =formats::optional,
      =welder::doc("Shadow batches (MOBS, Cata+).")]]
    std::vector<ShadowBatch> shadow_batches;

    [[=chunk("MDAL"), =since(wmo_ambient_override), =formats::optional,
      =welder::doc("Ambient color override (MDAL, WoD+); a single color in "
                   "practice, replacing the header ambient.")]]
    std::vector<CArgb> ambient_color_override;

    [[=chunk("MOPL"), =since(wmo_ambient_override), =formats::optional,
      =welder::doc("Terrain-cutting planes (MOPL, WoD+); requires the "
                   "can_cut_terrain flag, at most 32.")]]
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
      =welder::doc("Point lights (MOLP, Legion+).")]]
    std::vector<PointLight> point_lights;

    [[=chunk("MLSS"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Spot-light sets (MLSS, 8.1+): (first, count) ranges into "
                   "MOLS per doodad set.")]]
    std::vector<LightSet> spot_light_sets;

    [[=chunk("MLSP"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Point-light sets (MLSP, 8.1+): (first, count) ranges into "
                   "MOLP per doodad set.")]]
    std::vector<LightSet> point_light_sets;

    [[=chunk("MLSK"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Point-light animation sets (MLSK, 8.1+): (first, count) "
                   "ranges into MOP2.")]]
    std::vector<LightSet> point_light_anim_sets;

    [[=chunk("MOP2"), =since(wmo_light_sets), =formats::optional,
      =welder::doc("Animated point lights (MOP2, 8.1+).")]]
    std::vector<PointLightAnim> point_light_anims;

    [[=chunk("MPVR"), =since(wmo_volumes), =formats::optional,
      =welder::doc("Particulate volume references (MPVR, 8.3+) into the root's "
                   "MPVD.")]]
    std::vector<std::uint16_t> particulate_refs;

    [[=chunk("MAVR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Ambient volume references (MAVR, 9.0+) into the root's "
                   "MAVD.")]]
    std::vector<std::uint16_t> ambient_volume_refs;

    [[=chunk("MBVR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Box volume references (MBVR, 9.0+) into the root's MBVD.")]]
    std::vector<std::uint16_t> box_volume_refs;

    [[=chunk("MFVR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("Fog volume references (MFVR, 9.0+) into the root's MFOG "
                   "and MFED.")]]
    std::vector<std::uint16_t> fog_volume_refs;

    [[=chunk("MNLR"), =since(wmo_sl_extensions), =formats::optional,
      =welder::doc("New-light references (MNLR, 9.0+) into the root's MNLD.")]]
    std::vector<std::uint16_t> new_light_refs;

    // v14-alpha-only subchunks (MOLM/MOLD, MOIN, lightmap MOLV, MPB*) predate
    // the supported range; anything else unmodeled stays in
    // ChunkExtras::unknown, round-tripping verbatim.
  };

  /** One group file: MVER plus the MOGP container. */
  template <ClientVersion V>
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One WMO group file: the format version and the MOGP container "
                 "holding the geometry.")
  ]]
  WMOGroup : ChunkedFile<WMOGroup<V>>
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
