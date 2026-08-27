#pragma once

/** @file
    The WMO root-file entity (namespace wowlib::formats::wmo::root): header,
    materials, group metadata, portals, lights, doodads, fog and the
    later-expansion volume/light extensions. Version-gated chunks live in
    conditionally-inherited trait bases (one unwelded struct per availability
    range, in wmo::root::detail) — e.g. the pre-8.3 by-name blocks MOTX/MODN
    (gone once the client stopped resolving files by name) — while the
    always-present chunks are the
    entity's own members. A version's WMORoot therefore carries ONLY the chunks that version
    defines. The root binary structs live in wmo::root::chunks. */

#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>
#include <wowlib/formats/wmo/root/chunks/doodad.hpp>
#include <wowlib/formats/wmo/root/chunks/environment.hpp>
#include <wowlib/formats/wmo/root/chunks/header.hpp>
#include <wowlib/formats/wmo/root/chunks/light.hpp>
#include <wowlib/formats/wmo/root/chunks/material.hpp>
#include <wowlib/formats/wmo/root/chunks/structure.hpp>

namespace wowlib::formats::wmo::root {
  using namespace wowlib::formats::wmo::root::chunks;

  /** The version-agnostic base of every WMORoot<V> (welded as "WMORoot").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WMORoot* classes a common welded supertype, so binding
      users can write version-agnostic code (`isinstance(x, WMORoot)`, a
      `x: WMORoot` annotation, `WMORoot.for_version(expansion)`). It has no role in
      the C++ API, where you use the concrete WMORoot<V> directly.

      @see https://wowdev.wiki/WMO */
  struct [[
      =welder::weld,
      =welder::weld_as("WMORoot"),
      WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A WMO root file, abstract over the client version. The root file lists the
        object's shared data (materials, doodads, portals, lights, fog and the
        group table); the geometry lives in the separate group files. Construct a
        concrete version with WMORoot.for_version(expansion); the per-version
        WMORoot* classes are subclasses. See https://wowdev.wiki/WMO.)")
    ]] WMORootBase {};

  namespace detail {
    // --- version-range trait bases (unwelded) ---------------------------------
    // One struct per availability range; members keep their chunk/since/until/doc/
    // marks (read off the declaring class, so flattening preserves them). Member
    // order within a trait is for readability; the serialization order is the
    // entity's ChunkOrder table, not the flatten order.

    /** legion+ (7.0.1) root chunks. */
    struct RootLegion {
      [[
        =chunk("GFID"),
        =since(builds::Legion_Alpha),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Group file FileDataIDs (GFID, Legion+), in group order;
                        repeated per LOD level for LOD WMOs.)")]]
      std::vector<std::uint32_t> groupFdids;
    };

    /** 7.3+ root chunks. */
    struct Root73 {
      [[
        =chunk("MOUV"),
        =since(builds::Legion_ShadowsOfArgus_24473),
        =formats::Optional,
        =formats::countMatches("materials"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Texture-coordinate translation animations (MOUV, 7.3+), one
                        per material.)")]]
      std::vector<UVAnimation> uvAnimations;
    };

    /** Pre-8.3 root chunks: the by-name file-reference blocks. Since 8.3 the client
        no longer resolves textures or doodads by filename, so both are gone from
        8.3+ files — the material texture FileDataIDs and MODI (doodad FileDataIDs)
        fully replace them. (Assumption pending full-client validation; if a 8.3+
        file is ever seen carrying MOTX/MODN this bound is wrong.) */
    struct RootPre83 {
      [[
        =chunk("MOTX"),
        =until(builds::BfA_VisionsOfNzoth_32044),
        =formats::Optional,
        =welder::doc(
          R"(Texture filenames (MOTX); the material texture fields index
                        into this block. Pre-8.1 it is the primary reference; in
                        8.1/8.2 its presence marks the fallback mode (MOMT texture
                        fields are MOTX offsets, not FileDataIDs). Removed at 8.3,
                        when the client dropped name-based texture loading.)")]]
      StringBlock textures;

      [[
        =chunk("MODN"),
        =until(builds::BfA_VisionsOfNzoth_32044),
        =formats::Optional,
        =welder::doc(
          R"(Doodad (M2) filenames (MODN); MODD entries index into this
                        block. Pre-8.1 primary, 8.1/8.2 fallback (see textures).
                        Removed at 8.3 alongside MOTX; MODI (doodad FileDataIDs)
                        replaces it.)")]]
      StringBlock doodadNames;
    };

    /** 8.1+ root chunks (the FileDataID chunks that replace name lookups). */
    struct Root81 {
      [[
        =chunk("MOSI"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Skybox FileDataID (MOSI, 8.1+).")]]
      std::vector<std::uint32_t> skyboxFdid;

      [[
        =chunk("MODI"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          "Doodad FileDataIDs (MODI, 8.1+; replaces doodad_names).")]]
      std::vector<std::uint32_t> doodadFdids;
    };

    /** 8.3+ root chunks (per-doodad colour + the volume family). */
    struct Root83 {
      [[
        =chunk("MDDI"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::Optional,
        =formats::countMatches("doodadDefs"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Per-doodad color multipliers (MDDI, 8.3+), applied to the
                        MODD color.)")]]
      std::vector<float> doodadColorMults;

      [[
        =chunk("MPVD"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::Optional,
        =welder::doc(
          R"(Particulate volume data (MPVD, 8.3+); undocumented layout,
                        kept opaque.)")]]
      ChunkBlob particulateVolumes;

      [[
        =chunk("MAVG"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Global ambient volumes (MAVG, 8.3+); position and radii are
                        zero, selected by doodad set.)")]]
      std::vector<AmbientVolume> globalAmbientVolumes;

      [[
        =chunk("MAVD"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Ambient volumes (MAVD, 8.3+), overriding the header ambient
                        color inside their radius.)")]]
      std::vector<AmbientVolume> ambientVolumes;

      [[
        =chunk("MBVD"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Box ambient volumes (MBVD, 8.3+); read only when MAVG/MAVD is
                        present.)")]]
      std::vector<AmbientBoxVolume> ambientBoxVolumes;
    };

    /** 9.0+ root chunks (fog/group v2, dynamic lights, detail doodads). */
    struct Root90 {
      [[
        =chunk("MFED"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =formats::countMatches("fogs"),
        =welder::mark::no_reassign,
        =welder::doc("Fog extra data (MFED, 9.0+); same count as MFOG.")]]
      std::vector<FogExtra> fogExtras;

      [[
        =chunk("MGI2"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =formats::countMatches("groupInfos"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Group info v2 (MGI2, 9.0+); same count as MOGI, overrides LOD
                        selection.)")]]
      std::vector<GroupInfo2> groupInfos2;

      [[
        =chunk("MNLD"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Dynamic lights (MNLD, 9.0+): torch fires, window light
                        projection and the like.)")]]
      std::vector<NewLight> newLights;

      [[
        =chunk("MDDL"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =welder::doc(R"(Detail (ground-effect) doodad layers (MDDL, 9.0+);
                        variable-length RLE layout, kept opaque.)")]]
      ChunkBlob detailDoodads;
    };

    /** 9.1+ root chunks. */
    struct Root91 {
      [[
        =chunk("MOLV"),
        =since(builds::SL_ChainsOfDomination),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Directional-gradient light extensions (MOLV, 9.1+); entries
                        reference lights by index.)")]]
      std::vector<LightExtension> lightExtensions;
    };

    /** 11.0+ root chunks. */
    struct Root110 {
      [[
        =chunk("MOM3"),
        =since(builds::TWW_Alpha),
        =formats::Optional,
        =welder::doc(
          R"(M3 materials (MOM3, 11.0+); when present, MOMT is ignored. An
                        m3SI blob, kept opaque.)")]]
      ChunkBlob materialsM3;
    };

    /** 11.1+ root chunks. */
    struct Root111 {
      [[
        =chunk("MOPE"),
        =since(builds::TWW_Undermined),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Portal extra data (MOPE, 11.1+).")]]
      std::vector<PortalExtra> portalExtras;
    };
  }

  namespace detail {
    // The annotated entity; instantiate through the canonicalizing alias
    // below, never directly. The trait bases share this namespace, so they
    // need no qualifier (a bare detail:: would be ambiguous against
    // chunks::detail via the using-directive).
    /** A WMO (world map object) root file for one client version.

        The root file holds everything shared across the object — the material and
        texture tables, doodad (M2) sets and placements, portals, lights, fog, the
        convex-volume/visible-block culling data and the per-group table (MOGI/GFID)
        — while the actual 3D geometry lives in the separate group files (WMOGroup).
        The always-present chunks are own members (canonical order); version-gated
        chunks are inherited from the detail:: trait bases active for @a V.

        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WMO */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          A WMO root file for one client version. The root lists the object's shared
          data — materials and textures, doodad (M2) sets and placements, portals,
          lights, fog, culling volumes and the per-group table — while the 3D
          geometry lives in the separate group files (see WMOGroup). An instance read
          from a client file rewrites byte-for-byte until modified. See
          https://wowdev.wiki/WMO.)")
      ]] WMORoot
      : ChunkedFile<WMORoot<V>>,
        WMORootBase,
        Slot<V, builds::Legion_Alpha, RootLegion>,
        Slot<V, builds::Legion_ShadowsOfArgus_24473, Root73>,
        Slot<V, ClientVersion{0, 0, 0, 0}, RootPre83, builds::BfA_VisionsOfNzoth_32044>,
        Slot<V, builds::BfA_TidesOfVengeance, Root81>,
        Slot<V, builds::BfA_VisionsOfNzoth_32044, Root83>,
        Slot<V, builds::SL_Alpha_33978, Root90>,
        Slot<V, builds::SL_ChainsOfDomination, Root91>,
        Slot<V, builds::TWW_Alpha, Root110>,
        Slot<V, builds::TWW_Undermined, Root111> {
      static constexpr ClientVersion Version = V;

      [[
        =chunk("MVER"),
        =formats::expectedValue(WmoVersionV17),
        =welder::doc("The WMO format version; 17 for every supported client.")]]
      std::uint32_t mver = WmoVersionV17;

      [[
        =chunk("MOHD"),
        =welder::doc("The root header (MOHD).")]]
      SMOHeader header{};

      [[
        =chunk("MOMT"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Materials (MOMT).")]]
      std::vector<SMOMaterial> materials;

      [[
        =chunk("MOGN"),
        =formats::Optional,
        =welder::doc("Group names (MOGN), referenced by byte offset.")]]
      StringBlock groupNames;

      [[
        =chunk("MOGI"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Per-group info (MOGI).")]]
      std::vector<SMOGroupInfo> groupInfos;

      [[
        =chunk("MOSB"),
        =formats::Optional,
        =welder::doc(
          R"(Skybox filename (MOSB); raw bytes — files pad it to 4-byte
                        alignment. skybox_fdid (MOSI, 8.1+) functionally replaces
                        it, but the chunk never left the corpus: a 9.2.7 client
                        survey (2026-08-08) found MOSB in 5404 of 8148 roots —
                        always empty, never alongside MOSI — so it stays on every
                        version as the no-skybox marker exporters still emit.)")
      ]]
      ChunkBlob skyboxName;

      [[
        =chunk("MOPV"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Portal vertices (MOPV).")]]
      std::vector<C3Vector> portalVertices;

      [[
        =chunk("MOPT"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Portals (MOPT).")]]
      std::vector<SMOPortal> portals;

      [[
        =chunk("MOPR"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Portal references from groups (MOPR).")]]
      std::vector<SMOPortalRef> portalRefs;

      [[
        =chunk("MOVV"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Visible block vertices (MOVV).")]]
      std::vector<C3Vector> visibleBlockVertices;

      [[
        =chunk("MOVB"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Visible blocks (MOVB).")]]
      std::vector<SMOVisibleBlock> visibleBlocks;

      [[
        =chunk("MOLT"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Lights (MOLT).")]]
      std::vector<SMOLight> lights;

      [[
        =chunk("MODS"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Doodad sets (MODS).")]]
      std::vector<SMODoodadSet> doodadSets;

      [[
        =chunk("MODD"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Doodad placements (MODD).")]]
      std::vector<SMODoodadDef> doodadDefs;

      [[
        =chunk("MFOG"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Fog volumes (MFOG).")]]
      std::vector<SMOFog> fogs;

      [[
        =chunk("MCVP"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc("Convex volume planes (MCVP); transports mostly.")]]
      std::vector<C4Plane> convexVolumePlanes;

      [[
        =chunk("MOMX"),
        =formats::Optional,
        =welder::doc("MOMX (11.x, undocumented purpose); preserved opaque.")]]
      ChunkBlob momx;

      /** The canonical chunk-stream order the serializer emits a fresh entity in —
          decoupled from the by-trait flatten order of the version bases. Lists every
          chunk member exactly once (checked at compile time by writeOrder).

          MFOB (12.1+, Midnight) postdates the supported client range; it and any
          other unmodeled root chunk round-trip through ChunkExtras::unknown. */
      static constexpr std::array ChunkOrder = {
        fourcc("MVER"),
        fourcc("MOHD"),
        fourcc("MOTX"),
        fourcc("MOMT"),
        fourcc("MOM3"),
        fourcc("MOUV"),
        fourcc("MOGN"),
        fourcc("MOGI"),
        fourcc("MOSB"),
        fourcc("MOSI"),
        fourcc("MOPV"),
        fourcc("MOPT"),
        fourcc("MOPR"),
        fourcc("MOPE"),
        fourcc("MOVV"),
        fourcc("MOVB"),
        fourcc("MOLT"),
        fourcc("MOLV"),
        fourcc("MODS"),
        fourcc("MODN"),
        fourcc("MODI"),
        fourcc("MODD"),
        fourcc("MFOG"),
        fourcc("MCVP"),
        fourcc("GFID"),
        fourcc("MDDI"),
        fourcc("MPVD"),
        fourcc("MAVG"),
        fourcc("MAVD"),
        fourcc("MBVD"),
        fourcc("MFED"),
        fourcc("MGI2"),
        fourcc("MNLD"),
        fourcc("MDDL"),
        fourcc("MOMX"),
      };

      /** Serializer hook (see writeEntity): stamp the derived MOHD counts into
          the just-written header payload — nGroups from the MOGI table,
          nPortals and nDoodadSets from their vectors. The counts real client
          files disagree with their containers on (nTextures, nLights,
          nDoodadDefs, nDoodadNames) stay stored fields; see SMOHeader.
          @param fourccMagic  the emitted chunk's id, in disk layout.
          @param payload the finished chunk payload, patched in place. */
      [[=welder::mark::exclude]]
      void patchChunk(std::uint32_t fourccMagic, std::span<std::byte> payload) const {
        if (fourccMagic != fourcc("MOHD") || payload.size() < sizeof(SMOHeader)) return;
        SMOHeader h;
        std::memcpy(&h, payload.data(), sizeof h);
        h.nGroups = static_cast<std::uint32_t>(groupInfos.size());
        h.nPortals = static_cast<std::uint32_t>(portals.size());
        h.nDoodadSets = static_cast<std::uint32_t>(doodadSets.size());
        std::memcpy(payload.data(), &h, sizeof h);
      }

      /** Validation hook (see detail::validateEntity): the root contracts the
          annotations cannot express — doodad-set/portal/visible-block ranges,
          string-block references and the doodad name/FileDataID resolution.
          The MOHD counts patchChunk derives (nGroups, nPortals,
          nDoodadSets) need no check — every write restamps them — and the
          counts real files ship stale (nTextures, nLights, nDoodadDefs,
          nDoodadNames; see SMOHeader) are deliberately not validated.
          Cross-entity contracts (MOGI vs the group files, group references
          into this root) are the assembly's validate().
          @param report the report findings land in. */
      [[=welder::mark::exclude]]
      void validateExtra(ValidationReport& report) const {
        // MODS: (startIndex, count) ranges into MODD
        for (std::size_t i = 0; i < doodadSets.size(); ++i)
          if (doodadSets[i].startIndex + doodadSets[i].count > doodadDefs.size())
            report.addError(std::format("doodad_sets[{}]", i),
                            std::format("range [{}, {}) overruns the {} doodad placements", doodadSets[i].startIndex,
                                        doodadSets[i].startIndex + doodadSets[i].count, doodadDefs.size()));

        // MODD name references: MODN byte offsets pre-8.3, MODI indices once
        // the names block is gone (8.1/8.2 carry both - an engaged MODN marks
        // the by-name fallback mode, so it stays authoritative)
        constexpr std::size_t maxReported = 8;
        std::size_t badNames = 0;
        for (std::size_t i = 0; i < doodadDefs.size(); ++i) {
          const std::uint32_t name = doodadDefs[i].nameIndex();
          bool resolvable = false;
          std::string_view space;
          std::size_t bound = 0;
          if constexpr (requires { this->doodadNames; })
            if (!this->doodadNames.empty()) {
              resolvable = name < this->doodadNames.size();
              space = "doodad_names blob byte";
              bound = this->doodadNames.size();
            }
          if constexpr (requires { this->doodadFdids; })
            if (space.empty()) {
              resolvable = name < this->doodadFdids.size();
              space = "doodad FileDataID";
              bound = this->doodadFdids.size();
            }
          if (space.empty()) {
            resolvable = false;
            space = "doodad name source (none engaged)";
          }
          if (!resolvable && ++badNames <= maxReported)
            report.addError(std::format("doodadDefs[{}]", i),
                            std::format("name reference {} does not resolve: {} count {}", name, space, bound));
        }
        if (badNames > maxReported)
          report.addError("doodadDefs", std::format(
                            "... and {} more unresolvable name " "references", badNames - maxReported));

        // MOPT: vertex ranges into MOPV
        for (std::size_t i = 0; i < portals.size(); ++i)
          if (portals[i].startVertex + portals[i].count > portalVertices.size())
            report.addError(std::format("portals[{}]", i),
                            std::format("vertex range [{}, {}) overruns the {} portal vertices",
                                        portals[i].startVertex, portals[i].startVertex + portals[i].count,
                                        portalVertices.size()));

        // MOPR: indices into MOPT and MOGI
        for (std::size_t i = 0; i < portalRefs.size(); ++i) {
          if (portalRefs[i].portalIndex >= portals.size())
            report.addError(std::format("portal_refs[{}]", i),
                            std::format("portal_index {} out of range: {} portals", portalRefs[i].portalIndex,
                                        portals.size()));
          if (portalRefs[i].groupIndex >= groupInfos.size())
            report.addError(std::format("portal_refs[{}]", i),
                            std::format("group_index {} out of range: {} groups", portalRefs[i].groupIndex,
                                        groupInfos.size()));
        }

        // MOVB: vertex ranges into MOVV
        for (std::size_t i = 0; i < visibleBlocks.size(); ++i)
          if (visibleBlocks[i].firstVertex + visibleBlocks[i].count > visibleBlockVertices.size())
            report.addError(std::format("visible_blocks[{}]", i), std::format(
                              "vertex range [{}, {}) overruns the {} visible block " "vertices",
                              visibleBlocks[i].firstVertex, visibleBlocks[i].firstVertex + visibleBlocks[i].count,
                              visibleBlockVertices.size()));

        // MOGI: group-name offsets into MOGN (-1 marks the unnamed group)
        for (std::size_t i = 0; i < groupInfos.size(); ++i)
          if (groupInfos[i].nameOffset >= 0 && static_cast<std::size_t>(groupInfos[i].nameOffset) >= groupNames.
            size())
            report.addError(std::format("groupInfos[{}]", i),
                            std::format("name offset {} out of range: {} blob bytes", groupInfos[i].nameOffset,
                                        groupNames.size()));

        // MOMT texture references are MOTX byte offsets while the names block
        // is engaged (pre-8.1 always; 8.1/8.2 fallback mode). Only texture1
        // is validated: real vanilla/TBC-era files ship raw float garbage in
        // the texture2/texture3 slots (corpus: Stormwind.wmo, Subway.wmo),
        // which the client evidently never dereferences.
        if constexpr (requires { this->textures; })
          if (!this->textures.empty())
            for (std::size_t i = 0; i < materials.size(); ++i)
              if (materials[i].texture1 >= this->textures.size())
                report.addError(std::format("materials[{}]", i),
                                std::format("texture_1 offset {} out of range: {} blob bytes", materials[i].texture1,
                                            this->textures.size()));

        // MOLV (9.1+): entries reference lights by index
        if constexpr (requires { this->lightExtensions; })
          for (std::size_t i = 0; i < this->lightExtensions.size(); ++i)
            if (const auto index = this->lightExtensions[i].lightIndex; index < 0 || static_cast<std::size_t>(index)
              >= lights.size())
              report.addError(std::format("light_extensions[{}]", i),
                              std::format("light_index {} out of range: {} lights", index, lights.size()));
      }
    };
  }

  /** A WMO root file — the canonicalizing face of detail::WMORoot: every
      client version maps to its range's first grid version
      (WmoRootPivots), so e.g. one instantiation serves Vanilla through
      WoD. */
  template <ClientVersion V>
  using WMORoot = detail::WMORoot<canonicalVersion(V, WmoRootPivots, WmoVersions)>;
}
