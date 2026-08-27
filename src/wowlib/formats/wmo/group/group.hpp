#pragma once

/** @file
    The WMO group-file entities (namespace wowlib::formats::wmo::group): the
    MOGP container body (geometry, batches, collision, liquid and the
    later-expansion light/volume references) and the group file wrapping it. The
    body's version-gated chunks live in conditionally-inherited trait bases — one
    unwelded struct per availability range, in wmo::group::detail — while the
    always-present chunks are the body's own members. A version's WMOGroupBody
    therefore carries ONLY the fields that version has (setting an absent one is a
    compile error), and welder flattens the active traits' members onto its
    binding. The group binary structs are in wmo::group::chunks. */

#include <array>
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>
#include <wowlib/formats/wmo/group/chunks/geometry.hpp>
#include <wowlib/formats/wmo/group/chunks/header.hpp>
#include <wowlib/formats/wmo/group/chunks/light.hpp>
#include <wowlib/formats/wmo/group/chunks/liquid.hpp>

namespace wowlib::formats::wmo::group {
  using namespace wowlib::formats::wmo::group::chunks;

  /** The version-agnostic base of every WMOGroupBody<V> (welded as
      "WMOGroupBody").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WMOGroupBody* classes a common welded supertype so
      binding users can write version-agnostic code. It has no role in the C++
      API, where you use the concrete WMOGroupBody<V> directly.

      @see https://wowdev.wiki/WMO */
  struct [[
      =welder::weld,
      =welder::weld_as("WMOGroupBody"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        The MOGP container payload, abstract over the client version. Construct a
        concrete version with WMOGroupBody.for_version(expansion).)")
    ]] WMOGroupBodyBase {};

  /** The version-agnostic base of every WMOGroup<V> (welded as "WMOGroup").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WMOGroup* classes a common welded supertype so binding
      users can write version-agnostic code (`isinstance(x, WMOGroup)`, a
      `x: WMOGroup` annotation, `WMOGroup.for_version(expansion)`). It has no role
      in the C++ API, where you use the concrete WMOGroup<V> directly.

      @see https://wowdev.wiki/WMO */
  struct [[
      =welder::weld,
      =welder::weld_as("WMOGroup"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        One WMO group file, abstract over the client version. A group file holds
        the 3D model data (geometry, render batches, collision, liquid) for one
        unit of a world map object. Construct a concrete version with
        WMOGroup.for_version(expansion); the per-version WMOGroup* classes are
        subclasses. See https://wowdev.wiki/WMO.)")
    ]] WMOGroupBase {};

  namespace detail {
    // --- version-range trait bases (unwelded) ---------------------------------
    // One struct per availability range; members keep their chunk/since/doc/marks
    // (read off the declaring class, so flattening preserves them). Members are in
    // canonical order within a trait for readability; the serialization order is
    // the entity's ChunkOrder table, not the flatten order.

    /** cata+ (4.0) group-body chunks. */
    struct GroupBodyCata {
      [[
        =chunk("MORB"),
        =since(builds::Cata),
        =formats::Optional,
        =formats::countMatches("batches"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Triangle-strip batch overrides (MORB, Cata+); same count as
                        MOBA.)")]]
      std::vector<RenderBatchOverride> batchOverrides;

      [[
        =chunk("MOTA"),
        =since(builds::Cata),
        =formats::Optional,
        =welder::doc(R"(Tangent arrays (MOTA, Cata+); offset-based layout, kept
                        opaque.)")]]
      ChunkBlob tangents;

      [[
        =chunk("MOBS"),
        =since(builds::Cata),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Shadow batches (MOBS, Cata+).")]]
      std::vector<ShadowBatch> shadowBatches;
    };

    /** MoP+ (5.0) group-body chunks. */
    struct GroupBodyMop {
      [[
        =chunk("MDAL"),
        =since(builds::MoP),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Ambient color override (MDAL, MoP+); a single color in
                        practice, replacing the header ambient. wowdev dates it
                        to WoD but flags that unverified — a 5.4.8 corpus sweep
                        found 421 of these, while sampled Cata and WotLK groups
                        carry none.)")]]
      std::vector<CArgb> ambientColorOverride;
    };

    /** WoD+ (6.0) group-body chunks. */
    struct GroupBodyWod {
      [[
        =chunk("MOPL"),
        =since(builds::WoD),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Terrain-cutting planes (MOPL, WoD+); requires the
                        can_cut_terrain flag, at most 32.)")]]
      std::vector<C4Plane> terrainCutPlanes;
    };

    /** legion+ (7.0.1) group-body chunks. */
    struct GroupBodyLegion {
      [[
        =chunk("MOPB"),
        =since(builds::Legion_Alpha),
        =formats::Optional,
        =welder::doc(R"(Prepass batches (MOPB, Legion+); undocumented 24-byte
                        records, kept opaque.)")]]
      ChunkBlob prepassBatches;

      [[
        =chunk("MOLS"),
        =since(builds::Legion_Alpha),
        =formats::Optional,
        =welder::doc(
          R"(Spot lights (MOLS, Legion+); undocumented 56-byte records,
                        kept opaque.)")]]
      ChunkBlob spotLights;

      [[
        =chunk("MOLP"),
        =since(builds::Legion_Alpha),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Point lights (MOLP, Legion+).")]]
      std::vector<PointLight> pointLights;
    };

    /** 8.1+ group-body light-set chunks. */
    struct GroupBody81 {
      [[
        =chunk("MLSS"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Spot-light sets (MLSS, 8.1+): (first, count) ranges into MOLS
                        per doodad set.)")]]
      std::vector<LightSet> spotLightSets;

      [[
        =chunk("MLSP"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Point-light sets (MLSP, 8.1+): (first, count) ranges into
                        MOLP per doodad set.)")]]
      std::vector<LightSet> pointLightSets;

      [[
        =chunk("MLSK"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Point-light animation sets (MLSK, 8.1+): (first, count)
                        ranges into MOP2.)")]]
      std::vector<LightSet> pointLightAnimSets;

      [[
        =chunk("MOP2"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Animated point lights (MOP2, 8.1+).")]]
      std::vector<PointLightAnim> pointLightAnims;
    };

    /** 8.3+ group-body chunks. */
    struct GroupBody83 {
      [[
        =chunk("MPVR"),
        =since(builds::BfA_VisionsOfNzoth_33775),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          "Particulate volume references (MPVR, 8.3+) into the root's MPVD.")]]
      std::vector<std::uint16_t> particulateRefs;
    };

    /** 9.0+ group-body chunks (large-mesh indices and the volume/light refs). */
    struct GroupBody90 {
      [[
        =chunk("MOVX"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =formats::countMultipleOf(3),
        =formats::indexes("vertices"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(32-bit triangle vertex indices (MOVX, ~9.0+; the large-mesh
                        MOVI replacement).)")]]
      std::vector<std::uint32_t> largeIndices;

      [[
        =chunk("MAVR"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =formats::indexesInRoot("ambientVolumes"),
        =welder::mark::no_reassign,
        =welder::doc(
          "Ambient volume references (MAVR, 9.0+) into the root's MAVD.")]]
      std::vector<std::uint16_t> ambientVolumeRefs;

      [[
        =chunk("MBVR"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =formats::indexesInRoot("ambientBoxVolumes"),
        =welder::mark::no_reassign,
        =welder::doc("Box volume references (MBVR, 9.0+) into the root's MBVD.")
      ]]
      std::vector<std::uint16_t> boxVolumeRefs;

      [[
        =chunk("MFVR"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =formats::indexesInRoot("fogs"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Fog volume references (MFVR, 9.0+) into the root's MFOG and
                        MFED.)")]]
      std::vector<std::uint16_t> fogVolumeRefs;

      [[
          =chunk("MNLR"),
          =since(builds::SL_Alpha_33978),
          =formats::Optional,
          =formats::indexesInRoot("newLights"),
          =welder::mark::no_reassign,
          =welder::doc(
            "New-light references (MNLR, 9.0+) into the root's MNLD.")]
      ]
      std::vector<std::uint16_t> newLightRefs;
    };

    /** 10.0+ (Dragonflight) group-body query chunks. */
    struct GroupBody100 {
      [[
          =chunk("MOGX"),
          =since(builds::DF_Alpha),
          =formats::Optional,
          =welder::mark::no_reassign,
          =welder::doc(
            R"(Query face start (MOGX, 10.0+): the base subtracted from a
                        polygon index into MOQG. A single value in practice.)")]
      ]
      std::vector<std::uint32_t> queryFaceStart;

      [[
        =chunk("MPY2"),
        =since(builds::DF_Alpha),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          "Per-triangle material info v2 (MPY2, 10.0+; replaces MOPY).")]]
      std::vector<Poly2> polys2;

      [[
        =chunk("MOQG"),
        =since(builds::DF_Alpha),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Per-polygon ground types (MOQG, 10.0+), indexed by polygon
                        index minus the MOGX base.)")]]
      std::vector<std::uint32_t> queryFaces;
    };
  }

  namespace detail {
    // The annotated entities; instantiate through the canonicalizing
    // aliases below, never directly. The trait bases share this namespace,
    // so they need no qualifier (a bare detail:: would be ambiguous against
    // chunks::detail via the using-directive).
    /** The MOGP container payload for one client version.

        MOGP wraps the geometry that makes up one WMO group: the group header
        followed by the triangle/vertex/normal arrays, texture coordinates, render
        batches, the collision BSP tree, liquid grid and — on later clients — the
        group-local light and volume reference chunks. The always-present chunks are
        own members (canonical order); version-gated chunks are inherited from the
        detail:: trait bases active for @a V.

        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WMO */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          The MOGP payload for one client version: the group header and the geometry
          subchunks (triangles, vertices, normals, batches, BSP, liquid, lights).
          See https://wowdev.wiki/WMO.)")
      ]] WMOGroupBody
      : ChunkedFile<WMOGroupBody<V>>,
        WMOGroupBodyBase,
        Slot<V, builds::Cata, GroupBodyCata>,
        Slot<V, builds::MoP, GroupBodyMop>,
        Slot<V, builds::WoD, GroupBodyWod>,
        Slot<V, builds::Legion_Alpha, GroupBodyLegion>,
        Slot<V, builds::BfA_TidesOfVengeance, GroupBody81>,
        Slot<V, builds::BfA_VisionsOfNzoth_33775, GroupBody83>,
        Slot<V, builds::SL_Alpha_33978, GroupBody90>,
        Slot<V, builds::DF_Alpha, GroupBody100> {
      static constexpr ClientVersion Version = V;

      [[
        =formats::Header,
        =welder::doc("The group header leading the MOGP payload.")]]
      SMOGroupHeader<V> header{};

      // NOT countMatches("indices", 3): a 9.0+ large-mesh group carries its
      // indices in MOVX instead, leaving MOVI empty — the poly/face
      // relationship is checked against whichever list is active, in
      // validateExtra.
      [[
        =chunk("MOPY"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Per-triangle material info (MOPY).")]]
      std::vector<SMOPoly> polys;

      [[
        =chunk("MOVI"),
        =formats::Optional,
        =formats::countMultipleOf(3),
        =formats::indexes("vertices"),
        =welder::mark::no_reassign,
        =welder::doc("Triangle vertex indices (MOVI), three per triangle.")]]
      std::vector<std::uint16_t> indices;

      [[
        =chunk("MOVT"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Vertices (MOVT).")]]
      std::vector<C3Vector> vertices;

      [[
        =chunk("MONR"),
        =formats::Optional,
        =formats::countMatches("vertices"),
        =welder::mark::no_reassign,
        =welder::doc("Normals (MONR).")]]
      std::vector<C3Vector> normals;

      [[
        =chunk("MOTV"),
        =formats::Optional,
        =repeats(4),
        =formats::countMatches("vertices"),
        =welder::mark::only(welder::lang::py),
        =welder::doc(
          R"(Texture-coordinate sets (MOTV), up to four; the active count
                        is driven by the group flags (has_two_motv, has_three_motv).
                        Dragonflight groups ship a fourth set with no known flag
                        (983 roots' groups in the 10.2.7 fleet client — e.g.
                        Stormwind 8sw_portalroom01 group 2); it engages the
                        fourth slot here. Binds as a list of the filled sets,
                        by value.)")]]
      Repeated<std::vector<C2Vector>, 4> texcoords;

      [[
        =chunk("MOBA"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Render batches (MOBA).")]]
      std::vector<SMOBatch<V>> batches;

      [[
        =chunk("MOLR"),
        =formats::Optional,
        =formats::indexesInRoot("lights"),
        =welder::mark::no_reassign,
        =welder::doc("Light references into the root's MOLT (MOLR).")]]
      std::vector<std::uint16_t> lightRefs;

      [[
        =chunk("MODR"),
        =formats::Optional,
        =formats::indexesInRoot("doodadDefs"),
        =welder::mark::no_reassign,
        =welder::doc("Doodad references into the root's MODD (MODR).")]]
      std::vector<std::uint16_t> doodadRefs;

      [[
        =chunk("MOBN"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Collision BSP nodes (MOBN).")]]
      std::vector<CAaBspNode> bspNodes;

      [[
        =chunk("MOBR"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("BSP face indices (MOBR).")]]
      std::vector<std::uint16_t> bspFaceIndices;

      [[
        =chunk("MOCV"),
        =formats::Optional,
        =repeats(2),
        =formats::countMatches("vertices"),
        =welder::mark::only(welder::lang::py),
        =welder::doc(
          R"(Vertex-color layers (MOCV), up to two; the active count is
                        driven by the group flags (has_vertex_colors, has_two_mocv).
                        Binds as a list of the filled layers, by value.)")]]
      Repeated<std::vector<CImVector>, 2> vertexColors;

      [[
        =chunk("MOC2"),
        =formats::Optional,
        =formats::countMatches("vertices"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Second vertex-color-like weights (MOC2), used by the parallax
                        and shader-23 materials.)")]]
      std::vector<CImVector> vertexColors2;

      [[
        =chunk("MLIQ"),
        =formats::Optional,
        =welder::doc(
          R"(Liquid data (MLIQ): a vertex grid and tile-flag grid with a
                        base position and material id.)")]]
      MLIQData liquid;

      [[
        =chunk("MORI"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Triangle-strip indices (MORI).")]]
      std::vector<std::uint16_t> transBatchIndices;

      /** The canonical chunk-stream order the serializer emits a fresh entity in —
          decoupled from the by-trait flatten order of the version bases. Lists every
          chunk member exactly once (checked at compile time by writeOrder). The
          header (MOGP prelude) is written ahead of the stream, so it is not listed.

          v14-alpha-only subchunks (MOLM/MOLD, MOIN, lightmap MOLV, MPB*) predate the
          supported range; anything else unmodeled round-trips via ChunkExtras. */
      static constexpr std::array ChunkOrder = {
        fourCc("MOGX"),
        fourCc("MOPY"),
        fourCc("MPY2"),
        fourCc("MOVI"),
        fourCc("MOVX"),
        fourCc("MOVT"),
        fourCc("MONR"),
        fourCc("MOTV"),
        fourCc("MOBA"),
        fourCc("MOQG"),
        fourCc("MOLR"),
        fourCc("MODR"),
        fourCc("MOBN"),
        fourCc("MOBR"),
        fourCc("MOCV"),
        fourCc("MOC2"),
        fourCc("MLIQ"),
        fourCc("MORI"),
        fourCc("MORB"),
        fourCc("MOTA"),
        fourCc("MOBS"),
        fourCc("MDAL"),
        fourCc("MOPL"),
        fourCc("MOPB"),
        fourCc("MOLS"),
        fourCc("MOLP"),
        fourCc("MLSS"),
        fourCc("MLSP"),
        fourCc("MLSK"),
        fourCc("MOP2"),
        fourCc("MPVR"),
        fourCc("MAVR"),
        fourCc("MBVR"),
        fourCc("MFVR"),
        fourCc("MNLR"),
      };

      /** The number of triangle vertex indices the group's batches and BSP
          reference: MOVX (the large-mesh replacement) when engaged, MOVI
          otherwise.
          @return the active index count. */
      [[=welder::mark::exclude]]
      std::size_t activeIndexCount() const {
        if constexpr (requires { this->largeIndices; })
          if (!this->largeIndices.empty())
            return this->largeIndices.size();
        return indices.size();
      }

      /** Validation hook (see detail::validateEntity): the group contracts
          the annotations cannot express — render-batch and BSP index ranges,
          light-set ranges, header-flag/chunk-presence coherence and the MLIQ
          grid arithmetic. Cross-entity contracts (references into the root)
          are the assembly's validate().
          @param report the report findings land in. */
      [[=welder::mark::exclude]]
      void validateExtra(ValidationReport& report) const {
        const std::size_t indexCount = activeIndexCount();

        // one per-triangle record per three indices, against whichever index
        // list is active (MOVI, or MOVX on a large mesh)
        const auto perTriangle = [&](const auto& records,
                                      std::string_view what) {
          if (!records.empty() && records.size() * 3 != indexCount)
            report.addError(std::string{what},
                             std::format(
                               "count {} x 3 != the {} active triangle indices",
                               records.size(), indexCount));
        };
        perTriangle(polys, "polys");
        if constexpr (requires { this->polys2; })
          perTriangle(this->polys2, "polys2");

        // MOBA ranges: the client draws [startIndex, startIndex + count) and
        // uploads vertices up to maxIndex
        for (std::size_t i = 0; i < batches.size(); ++i) {
          const auto& batch = batches[i];
          if (batch.startIndex + batch.count > indexCount)
            report.addError(std::format("batches[{}]", i),
                             std::format(
                               "index range [{}, {}) overruns the {} indices",
                               batch.startIndex,
                               batch.startIndex + batch.count,
                               indexCount));
          if (batch.minIndex > batch.maxIndex)
            report.addError(std::format("batches[{}]", i),
                             std::format("min_index {} > max_index {}",
                                         batch.minIndex,
                                         batch.maxIndex));
          else if (!vertices.empty() && batch.maxIndex >= vertices.size())
            report.addError(std::format("batches[{}]", i),
                             std::format(
                               "max_index {} out of range: {} vertices",
                               batch.maxIndex, vertices.size()));
        }

        // the header's batch partition counts what MOBA holds
        const std::size_t declaredBatches = static_cast<std::size_t>(header.
            transBatchCount)
          + header.intBatchCount + header.extBatchCount;
        if (declaredBatches != batches.size())
          report.addError("header",
                           std::format("batch counts {}+{}+{} != {} batches",
                                       header.transBatchCount,
                                       header.intBatchCount,
                                       header.extBatchCount, batches.size()));

        // BSP: leaves reference faces through MOBR; children index MOBN
        const std::size_t faceCount = indexCount / 3;
        formats::detail::validateIndexElements(
          bspFaceIndices, faceCount, "bspFaceIndices",
          "faces", report);
        for (std::size_t i = 0; i < bspNodes.size(); ++i) {
          const auto& node = bspNodes[i];
          for (const std::int16_t child : {node.negChild, node.posChild})
            if (child != -1 && static_cast<std::size_t>(child) >= bspNodes.
              size())
              report.addError(std::format("bsp_nodes[{}]", i),
                               std::format("child {} out of range: {} nodes",
                                           child,
                                           bspNodes.size()));
          if (node.faceStart + node.nFaces > bspFaceIndices.size())
            report.addError(std::format("bsp_nodes[{}]", i),
                             std::format(
                               "face range [{}, {}) overruns the {} face indices",
                               node.faceStart, node.faceStart + node.nFaces,
                               bspFaceIndices.size()));
        }

        // header flags vs chunk presence: the base HasVertexColors flag with
        // NO layer at all makes the client consume absent data (error); the
        // multi-layer flags fall short in real files (corpus: a BfA kultiras
        // group ships HasTwoMocv with a single layer), so a shortfall there
        // only warns
        const auto flagWants = [&](GroupFlags flag,
                                    std::size_t have,
                                    std::size_t want,
                                    std::string_view what,
                                    ValidationSeverity severity) {
          if (hasFlag(header.flags, flag) && have < want)
            report.add(severity, std::string{what},
                       std::format(
                         "group flag {:#x} is set but only {} of {} {} present",
                         std::to_underlying(flag), have, want, what));
        };
        flagWants(GroupFlags::HasVertexColors, vertexColors.size(), 1,
                   "vertexColors",
                   ValidationSeverity::Error);
        flagWants(GroupFlags::HasTwoMocv, vertexColors.size(), 2,
                   "vertexColors",
                   ValidationSeverity::Warning);
        flagWants(GroupFlags::HasTwoMotv, texcoords.size(), 2, "texcoords",
                   ValidationSeverity::Warning);
        flagWants(GroupFlags::HasThreeMotv, texcoords.size(), 3, "texcoords",
                   ValidationSeverity::Warning);
        if (!vertexColors.empty() && !hasFlag(
          header.flags, GroupFlags::HasVertexColors))
          report.addWarning("vertexColors",
                             "present but the has_vertex_colors group flag is clear");

        // MLIQ: the grids must match their declared dimensions, and the flag
        // decides whether the client reads the chunk at all
        if (!liquid.empty()) {
          const auto expect = [&](const auto& grid,
                                  const C2IVector& dim,
                                  std::string_view what) {
            const std::size_t cells =
              dim.x < 0 || dim.y < 0
                ? 0
                : static_cast<std::size_t>(dim.x) * static_cast<std::size_t>(dim
                  .y);
            if (grid.size() != cells)
              report.addError(std::format("liquid.{}", what),
                               std::format("count {} != {}x{} grid",
                                           grid.size(), dim.x, dim.y));
          };
          expect(liquid.vertices, liquid.vertsDim, "vertices");
          expect(liquid.tiles, liquid.tilesDim, "tiles");
          if (liquid.vertsDim.x != liquid.tilesDim.x + 1
            || liquid.vertsDim.y != liquid.tilesDim.y + 1)
            report.addError("liquid",
                             std::format(
                               "vertex grid {}x{} is not one larger per axis than the "
                               "tile grid {}x{}",
                               liquid.vertsDim.x, liquid.vertsDim.y,
                               liquid.tilesDim.x, liquid.tilesDim.y));
          if (!hasFlag(header.flags, GroupFlags::HasLiquid))
            report.addWarning("liquid",
                               "present but the has_liquid group flag is clear");
        }

        // WoD+ terrain-cut planes: the client caps them at 32 and gates on flags2
        if constexpr (requires { this->terrainCutPlanes; }) {
          if (this->terrainCutPlanes.size() > 32)
            report.addError("terrainCutPlanes",
                             std::format(
                               "{} planes exceed the client's cap of 32",
                               this->terrainCutPlanes.size()));
          if (!this->terrainCutPlanes.empty()
            && !hasFlag(header.flags2, GroupFlags2::CanCutTerrain))
            report.addWarning("terrainCutPlanes",
                               "present but the can_cut_terrain flag2 is clear");
        }

        // 8.1+ light sets: (offset, count) ranges into their referenced chunks
        if constexpr (requires { this->pointLightSets; }) {
          const auto setRanges = [&](const auto& sets,
                                      const auto& target,
                                      std::string_view member,
                                      std::string_view what) {
            for (std::size_t i = 0; i < sets.size(); ++i)
              if (sets[i].offset + sets[i].count > target.size())
                report.addError(std::format("{}[{}]", member, i),
                                 std::format(
                                   "range [{}, {}) overruns the {} {}",
                                   sets[i].offset,
                                   sets[i].offset + sets[i].count,
                                   target.size(), what));
          };
          setRanges(this->pointLightSets, this->pointLights,
                     "pointLightSets",
                     "point lights");
          setRanges(this->pointLightAnimSets, this->pointLightAnims,
                     "pointLightAnimSets", "animated point lights");
        }
      }
    };

    /** One WMO group file for one client version: the format version (MVER) and the
        MOGP container holding the group's geometry.

        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WMO */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          One WMO group file for one client version: the format version and the MOGP
          container holding the group's geometry (see WMOGroupBody). A group holds
          the 3D model data for one unit of a world map object. See
          https://wowdev.wiki/WMO.)")
      ]] WMOGroup : ChunkedFile<WMOGroup<V>>, WMOGroupBase {
      static constexpr ClientVersion Version = V;

      [[
        =chunk("MVER"),
        =formats::expectedValue(WmoVersionV17),
        =welder::doc("The WMO format version; 17 for every supported client.")]]
      std::uint32_t mver = WmoVersionV17;

      [[
        =chunk("MOGP"),
        =formats::Container,
        =welder::doc("The MOGP container: group header and geometry.")]]
      // the raw sibling is intentional: WMOGroup and WMOGroupBody share
      // WmoGroupPivots, so at a canonical V they are the same type the
      // canonicalizing alias would name
      WMOGroupBody<V> body{};
    };
  }

  /** The MOGP payload — the canonicalizing face of detail::WMOGroupBody:
      every client version maps to its range's first grid version
      (WmoGroupPivots). */
  template <ClientVersion V>
  using WMOGroupBody =
  group::detail::WMOGroupBody<canonicalVersion(V, WmoGroupPivots,
                                                WmoVersions)>;

  /** One WMO group file — the canonicalizing face of detail::WMOGroup
      (same pivots as the body it contains). */
  template <ClientVersion V>
  using WMOGroup =
  group::detail::WMOGroup<canonicalVersion(V, WmoGroupPivots, WmoVersions)>;
}
