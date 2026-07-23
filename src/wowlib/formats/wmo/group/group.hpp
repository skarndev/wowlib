#pragma once

/** @file
    The WMO group-file entities (namespace wowlib::formats::wmo::group): the
    MOGP container body (geometry, batches, collision, liquid and the
    later-expansion light/volume references) and the group file wrapping it,
    declared in canonical client chunk order. The group wire structs it is built
    from live in wmo::group::chunks. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/serializer.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>
#include <wowlib/formats/wmo/group/chunks/geometry.hpp>
#include <wowlib/formats/wmo/group/chunks/header.hpp>
#include <wowlib/formats/wmo/group/chunks/light.hpp>
#include <wowlib/formats/wmo/group/chunks/liquid.hpp>

namespace wowlib::formats::wmo::group
{
  using namespace wowlib::formats::wmo::group::chunks;

  /** The version-agnostic base of every WMOGroupBody<V> (welded as
      "WMOGroupBody").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WMOGroupBody* classes a common welded supertype so
      binding users can write version-agnostic code. It has no role in the C++
      API, where you use the concrete WMOGroupBody<V> directly.

      @see https://wowdev.wiki/WMO */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMOGroupBody"),
    =welder::doc(R"(
        The MOGP container payload, abstract over the client version. Construct a
        concrete version with WMOGroupBody.for_version(expansion).)")
  ]] WMOGroupBodyBase
  {
  };

  /** The version-agnostic base of every WMOGroup<V> (welded as "WMOGroup").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WMOGroup* classes a common welded supertype so binding
      users can write version-agnostic code (`isinstance(x, WMOGroup)`, a
      `x: WMOGroup` annotation, `WMOGroup.for_version(expansion)`). It has no role
      in the C++ API, where you use the concrete WMOGroup<V> directly.

      @see https://wowdev.wiki/WMO */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMOGroup"),
    =welder::doc(R"(
        One WMO group file, abstract over the client version. A group file holds
        the 3D model data (geometry, render batches, collision, liquid) for one
        unit of a world map object. Construct a concrete version with
        WMOGroup.for_version(expansion); the per-version WMOGroup* classes are
        subclasses. See https://wowdev.wiki/WMO.)")
  ]] WMOGroupBase
  {
  };

  /** The MOGP container payload for one client version.

      MOGP wraps the geometry that makes up one WMO group: the group header
      followed by the triangle/vertex/normal arrays, texture coordinates, render
      batches, the collision BSP tree, liquid grid and — on later clients — the
      group-local light and volume reference chunks. Members are declared in
      canonical client chunk order.

      @tparam V the client version this layout targets.
      @see https://wowdev.wiki/WMO */
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The MOGP payload for one client version: the group header and the geometry
        subchunks (triangles, vertices, normals, batches, BSP, liquid, lights).
        See https://wowdev.wiki/WMO.)")
  ]] WMOGroupBody : ChunkedFile<WMOGroupBody<V>>, WMOGroupBodyBase
  {
    static constexpr ClientVersion version = V;

    [[
      =formats::header,
      =welder::doc("The group header leading the MOGP payload.")]]
    SMOGroupHeader<V> header{};

    [[
      =chunk("MOGX"),
      =since(ClientVersion{10, 0, 0, 46181}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Query face start (MOGX, 10.0+): the base subtracted from a
                      polygon index into MOQG. A single value in practice.)")]]
    std::vector<std::uint32_t> query_face_start;

    [[
      =chunk("MOPY"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Per-triangle material info (MOPY).")]]
    std::vector<SMOPoly> polys;

    [[
      =chunk("MPY2"),
      =since(ClientVersion{10, 0, 0, 46181}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Per-triangle material info v2 (MPY2, 10.0+; replaces MOPY).")]]
    std::vector<Poly2> polys2;

    [[
      =chunk("MOVI"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Triangle vertex indices (MOVI), three per triangle.")]]
    std::vector<std::uint16_t> indices;

    [[
      =chunk("MOVX"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(32-bit triangle vertex indices (MOVX, ~9.0+; the large-mesh
                      MOVI replacement).)")]]
    std::vector<std::uint32_t> large_indices;

    [[
      =chunk("MOVT"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Vertices (MOVT).")]]
    std::vector<C3Vector> vertices;

    [[
      =chunk("MONR"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Normals (MONR).")]]
    std::vector<C3Vector> normals;

    [[
      =chunk("MOTV"),
      =formats::optional,
      =repeats(3),
      =welder::mark::only(welder::lang::py),
      =welder::doc(R"(Texture-coordinate sets (MOTV), up to three; the active count
                      is driven by the group flags (has_two_motv, has_three_motv).
                      Binds as a list of the filled sets, by value.)")]]
    Repeated<std::vector<C2Vector>, 3> texcoords;

    [[
      =chunk("MOBA"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Render batches (MOBA).")]]
    std::vector<SMOBatch<V>> batches;

    [[
      =chunk("MOQG"),
      =since(ClientVersion{10, 0, 0, 46181}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Per-polygon ground types (MOQG, 10.0+), indexed by polygon
                      index minus the MOGX base.)")]]
    std::vector<std::uint32_t> query_faces;

    [[
      =chunk("MOLR"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Light references into the root's MOLT (MOLR).")]]
    std::vector<std::uint16_t> light_refs;

    [[
      =chunk("MODR"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Doodad references into the root's MODD (MODR).")]]
    std::vector<std::uint16_t> doodad_refs;

    [[
      =chunk("MOBN"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Collision BSP nodes (MOBN).")]]
    std::vector<CAaBspNode> bsp_nodes;

    [[
      =chunk("MOBR"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("BSP face indices (MOBR).")]]
    std::vector<std::uint16_t> bsp_face_indices;

    [[
      =chunk("MOCV"),
      =formats::optional,
      =repeats(2),
      =welder::mark::only(welder::lang::py),
      =welder::doc(R"(Vertex-color layers (MOCV), up to two; the active count is
                      driven by the group flags (has_vertex_colors, has_two_mocv).
                      Binds as a list of the filled layers, by value.)")]]
    Repeated<std::vector<CImVector>, 2> vertex_colors;

    [[
      =chunk("MOC2"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Second vertex-color-like weights (MOC2), used by the parallax
                      and shader-23 materials.)")]]
    std::vector<CImVector> vertex_colors2;

    [[
      =chunk("MLIQ"),
      =formats::optional,
      =welder::doc(R"(Liquid data (MLIQ): a vertex grid and tile-flag grid with a
                      base position and material id.)")]]
    MLIQData liquid;

    [[
      =chunk("MORI"),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Triangle-strip indices (MORI).")]]
    std::vector<std::uint16_t> trans_batch_indices;

    [[
      =chunk("MORB"),
      =since(ClientVersion{4, 0, 0, 0}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Triangle-strip batch overrides (MORB, Cata+); same count as
                      MOBA.)")]]
    std::vector<RenderBatchOverride> batch_overrides;

    [[
      =chunk("MOTA"),
      =since(ClientVersion{4, 0, 0, 0}),
      =formats::optional,
      =welder::doc(R"(Tangent arrays (MOTA, Cata+); offset-based layout, kept
                      opaque.)")]]
    ChunkBlob tangents;

    [[
      =chunk("MOBS"),
      =since(ClientVersion{4, 0, 0, 0}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Shadow batches (MOBS, Cata+).")]]
    std::vector<ShadowBatch> shadow_batches;

    [[
      =chunk("MDAL"),
      =since(ClientVersion{6, 0, 0, 0}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Ambient color override (MDAL, WoD+); a single color in
                      practice, replacing the header ambient.)")]]
    std::vector<CArgb> ambient_color_override;

    [[
      =chunk("MOPL"),
      =since(ClientVersion{6, 0, 0, 0}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Terrain-cutting planes (MOPL, WoD+); requires the
                      can_cut_terrain flag, at most 32.)")]]
    std::vector<C4Plane> terrain_cut_planes;

    [[
      =chunk("MOPB"),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::doc(R"(Prepass batches (MOPB, Legion+); undocumented 24-byte
                      records, kept opaque.)")]]
    ChunkBlob prepass_batches;

    [[
      =chunk("MOLS"),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::doc(R"(Spot lights (MOLS, Legion+); undocumented 56-byte records,
                      kept opaque.)")]]
    ChunkBlob spot_lights;

    [[
      =chunk("MOLP"),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Point lights (MOLP, Legion+).")]]
    std::vector<PointLight> point_lights;

    [[
      =chunk("MLSS"),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Spot-light sets (MLSS, 8.1+): (first, count) ranges into MOLS
                      per doodad set.)")]]
    std::vector<LightSet> spot_light_sets;

    [[
      =chunk("MLSP"),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Point-light sets (MLSP, 8.1+): (first, count) ranges into
                      MOLP per doodad set.)")]]
    std::vector<LightSet> point_light_sets;

    [[
      =chunk("MLSK"),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Point-light animation sets (MLSK, 8.1+): (first, count)
                      ranges into MOP2.)")]]
    std::vector<LightSet> point_light_anim_sets;

    [[
      =chunk("MOP2"),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Animated point lights (MOP2, 8.1+).")]]
    std::vector<PointLightAnim> point_light_anims;

    [[
      =chunk("MPVR"),
      =since(ClientVersion{8, 3, 0, 33775}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Particulate volume references (MPVR, 8.3+) into the root's MPVD.")]]
    std::vector<std::uint16_t> particulate_refs;

    [[
      =chunk("MAVR"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Ambient volume references (MAVR, 9.0+) into the root's MAVD.")]]
    std::vector<std::uint16_t> ambient_volume_refs;

    [[
      =chunk("MBVR"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Box volume references (MBVR, 9.0+) into the root's MBVD.")]]
    std::vector<std::uint16_t> box_volume_refs;

    [[
      =chunk("MFVR"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(R"(Fog volume references (MFVR, 9.0+) into the root's MFOG and
                      MFED.)")]]
    std::vector<std::uint16_t> fog_volume_refs;

    [[
      =chunk("MNLR"),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("New-light references (MNLR, 9.0+) into the root's MNLD.")]]
    std::vector<std::uint16_t> new_light_refs;

    // v14-alpha-only subchunks (MOLM/MOLD, MOIN, lightmap MOLV, MPB*) predate
    // the supported range; anything else unmodeled stays in
    // ChunkExtras::unknown, round-tripping verbatim.
  };

  /** One WMO group file for one client version: the format version (MVER) and the
      MOGP container holding the group's geometry.

      @tparam V the client version this layout targets.
      @see https://wowdev.wiki/WMO */
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One WMO group file for one client version: the format version and the MOGP
        container holding the group's geometry (see WMOGroupBody). A group holds
        the 3D model data for one unit of a world map object. See
        https://wowdev.wiki/WMO.)")
  ]] WMOGroup : ChunkedFile<WMOGroup<V>>, WMOGroupBase
  {
    static constexpr ClientVersion version = V;

    [[
      =chunk("MVER"),
      =welder::doc("The WMO format version; 17 for every supported client.")]]
    std::uint32_t mver = wmo_version_v17;

    [[
      =chunk("MOGP"),
      =formats::container,
      =welder::doc("The MOGP container: group header and geometry.")]]
    WMOGroupBody<V> body{};
  };
}
