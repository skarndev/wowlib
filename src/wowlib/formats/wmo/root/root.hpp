#pragma once

/** @file
    The WMO root-file entity (namespace wowlib::formats::wmo::root): header,
    materials, group metadata, portals, lights, doodads, fog and the
    later-expansion volume/light extensions. Version-gated chunks live in
    conditionally-inherited trait bases (one unwelded struct per availability
    range, in wmo::root::detail) — including the pre-8.1-only MOSB (replaced by
    the 8.1+ MOSI) and the pre-8.3 by-name blocks MOTX/MODN (gone once the client
    stopped resolving files by name) — while the always-present chunks are the
    entity's own members. A version's WMORoot therefore carries ONLY the chunks that version
    defines. The root wire structs live in wmo::root::chunks. */

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/serializer.hpp>
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

  namespace detail
  {
    // --- version-range trait bases (unwelded) ---------------------------------
    // One struct per availability range; members keep their chunk/since/until/doc/
    // marks (read off the declaring class, so flattening preserves them). Member
    // order within a trait is for readability; the serialization order is the
    // entity's chunk_order table, not the flatten order.

    /** Legion+ (7.0.1) root chunks. */
    struct RootLegion
    {
      [[
        =chunk("GFID"),
        =since(builds::Legion_Alpha),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Group file FileDataIDs (GFID, Legion+), in group order;
                        repeated per LOD level for LOD WMOs.)")]]
      std::vector<std::uint32_t> group_fdids;
    };

    /** 7.3+ root chunks. */
    struct Root73
    {
      [[
        =chunk("MOUV"),
        =since(builds::Legion_ShadowsOfArgus_24473),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Texture-coordinate translation animations (MOUV, 7.3+), one
                        per material.)")]]
      std::vector<UVAnimation> uv_animations;
    };

    /** Pre-8.1 root chunks, removed once their FileDataID counterparts arrive. */
    struct RootPre81
    {
      [[
        =chunk("MOSB"),
        =until(builds::BfA_TidesOfVengeance),
        =formats::optional,
        =welder::doc(R"(Skybox filename (MOSB; pre-8.1); raw bytes — files pad it to
                        4-byte alignment. Replaced by skybox_fdid (MOSI) in 8.1+.)")]]
      ChunkBlob skybox_name;
    };

    /** Pre-8.3 root chunks: the by-name file-reference blocks. Since 8.3 the client
        no longer resolves textures or doodads by filename, so both are gone from
        8.3+ files — the material texture FileDataIDs and MODI (doodad FileDataIDs)
        fully replace them. (Assumption pending full-client validation; if a 8.3+
        file is ever seen carrying MOTX/MODN this bound is wrong.) */
    struct RootPre83
    {
      [[
        =chunk("MOTX"),
        =until(builds::BfA_VisionsOfNzoth_32044),
        =formats::optional,
        =welder::doc(R"(Texture filenames (MOTX); the material texture fields index
                        into this block. Pre-8.1 it is the primary reference; in
                        8.1/8.2 its presence marks the fallback mode (MOMT texture
                        fields are MOTX offsets, not FileDataIDs). Removed at 8.3,
                        when the client dropped name-based texture loading.)")]]
      StringBlock textures;

      [[
        =chunk("MODN"),
        =until(builds::BfA_VisionsOfNzoth_32044),
        =formats::optional,
        =welder::doc(R"(Doodad (M2) filenames (MODN); MODD entries index into this
                        block. Pre-8.1 primary, 8.1/8.2 fallback (see textures).
                        Removed at 8.3 alongside MOTX; MODI (doodad FileDataIDs)
                        replaces it.)")]]
      StringBlock doodad_names;
    };

    /** 8.1+ root chunks (the FileDataID chunks that replace name lookups). */
    struct Root81
    {
      [[
        =chunk("MOSI"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Skybox FileDataID (MOSI, 8.1+).")]]
      std::vector<std::uint32_t> skybox_fdid;

      [[
        =chunk("MODI"),
        =since(builds::BfA_TidesOfVengeance),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Doodad FileDataIDs (MODI, 8.1+; replaces doodad_names).")]]
      std::vector<std::uint32_t> doodad_fdids;
    };

    /** 8.3+ root chunks (per-doodad colour + the volume family). */
    struct Root83
    {
      [[
        =chunk("MDDI"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Per-doodad color multipliers (MDDI, 8.3+), applied to the
                        MODD color.)")]]
      std::vector<float> doodad_color_mults;

      [[
        =chunk("MPVD"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::optional,
        =welder::doc(R"(Particulate volume data (MPVD, 8.3+); undocumented layout,
                        kept opaque.)")]]
      ChunkBlob particulate_volumes;

      [[
        =chunk("MAVG"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Global ambient volumes (MAVG, 8.3+); position and radii are
                        zero, selected by doodad set.)")]]
      std::vector<AmbientVolume> global_ambient_volumes;

      [[
        =chunk("MAVD"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Ambient volumes (MAVD, 8.3+), overriding the header ambient
                        color inside their radius.)")]]
      std::vector<AmbientVolume> ambient_volumes;

      [[
        =chunk("MBVD"),
        =since(builds::BfA_VisionsOfNzoth_32044),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Box ambient volumes (MBVD, 8.3+); read only when MAVG/MAVD is
                        present.)")]]
      std::vector<AmbientBoxVolume> ambient_box_volumes;
    };

    /** 9.0+ root chunks (fog/group v2, dynamic lights, detail doodads). */
    struct Root90
    {
      [[
        =chunk("MFED"),
        =since(builds::SL_Alpha_33978),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Fog extra data (MFED, 9.0+); same count as MFOG.")]]
      std::vector<FogExtra> fog_extras;

      [[
        =chunk("MGI2"),
        =since(builds::SL_Alpha_33978),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Group info v2 (MGI2, 9.0+); same count as MOGI, overrides LOD
                        selection.)")]]
      std::vector<GroupInfo2> group_infos2;

      [[
        =chunk("MNLD"),
        =since(builds::SL_Alpha_33978),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Dynamic lights (MNLD, 9.0+): torch fires, window light
                        projection and the like.)")]]
      std::vector<NewLight> new_lights;

      [[
        =chunk("MDDL"),
        =since(builds::SL_Alpha_33978),
        =formats::optional,
        =welder::doc(R"(Detail (ground-effect) doodad layers (MDDL, 9.0+);
                        variable-length RLE layout, kept opaque.)")]]
      ChunkBlob detail_doodads;
    };

    /** 9.1+ root chunks. */
    struct Root91
    {
      [[
        =chunk("MOLV"),
        =since(builds::SL_ChainsOfDomination),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Directional-gradient light extensions (MOLV, 9.1+); entries
                        reference lights by index.)")]]
      std::vector<LightExtension> light_extensions;
    };

    /** 11.0+ root chunks. */
    struct Root110
    {
      [[
        =chunk("MOM3"),
        =since(builds::TWW_Alpha),
        =formats::optional,
        =welder::doc(R"(M3 materials (MOM3, 11.0+); when present, MOMT is ignored. An
                        m3SI blob, kept opaque.)")]]
      ChunkBlob materials_m3;
    };

    /** 11.1+ root chunks. */
    struct Root111
    {
      [[
        =chunk("MOPE"),
        =since(builds::TWW_Undermined),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Portal extra data (MOPE, 11.1+).")]]
      std::vector<PortalExtra> portal_extras;
    };
  }

  namespace detail
  {
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
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc(R"(
          A WMO root file for one client version. The root lists the object's shared
          data — materials and textures, doodad (M2) sets and placements, portals,
          lights, fog, culling volumes and the per-group table — while the 3D
          geometry lives in the separate group files (see WMOGroup). An instance read
          from a client file rewrites byte-for-byte until modified. See
          https://wowdev.wiki/WMO.)")
    ]] WMORoot
      : ChunkedFile<WMORoot<V>>, WMORootBase,
        slot<V, builds::Legion_Alpha, RootLegion>,
        slot<V, builds::Legion_ShadowsOfArgus_24473, Root73>,
        slot<V, ClientVersion{0, 0, 0, 0}, RootPre81, builds::BfA_TidesOfVengeance>,
        slot<V, ClientVersion{0, 0, 0, 0}, RootPre83, builds::BfA_VisionsOfNzoth_32044>,
        slot<V, builds::BfA_TidesOfVengeance, Root81>,
        slot<V, builds::BfA_VisionsOfNzoth_32044, Root83>,
        slot<V, builds::SL_Alpha_33978, Root90>,
        slot<V, builds::SL_ChainsOfDomination, Root91>,
        slot<V, builds::TWW_Alpha, Root110>,
        slot<V, builds::TWW_Undermined, Root111>
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
        =chunk("MOMT"),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Materials (MOMT).")]]
      std::vector<SMOMaterial> materials;

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
        =chunk("MODS"),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Doodad sets (MODS).")]]
      std::vector<SMODoodadSet> doodad_sets;

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
        =chunk("MOMX"),
        =formats::optional,
        =welder::doc("MOMX (11.x, undocumented purpose); preserved opaque.")]]
      ChunkBlob momx;

      /** The canonical chunk-stream order the serializer emits a fresh entity in —
          decoupled from the by-trait flatten order of the version bases. Lists every
          chunk member exactly once (checked at compile time by write_order).

          MFOB (12.1+, Midnight) postdates the supported client range; it and any
          other unmodeled root chunk round-trip through ChunkExtras::unknown. */
      static constexpr std::array chunk_order = {
        four_cc("MVER"), four_cc("MOHD"), four_cc("MOTX"), four_cc("MOMT"), four_cc("MOM3"),
        four_cc("MOUV"), four_cc("MOGN"), four_cc("MOGI"), four_cc("MOSB"), four_cc("MOSI"),
        four_cc("MOPV"), four_cc("MOPT"), four_cc("MOPR"), four_cc("MOPE"), four_cc("MOVV"),
        four_cc("MOVB"), four_cc("MOLT"), four_cc("MOLV"), four_cc("MODS"), four_cc("MODN"),
        four_cc("MODI"), four_cc("MODD"), four_cc("MFOG"), four_cc("MCVP"), four_cc("GFID"),
        four_cc("MDDI"), four_cc("MPVD"), four_cc("MAVG"), four_cc("MAVD"), four_cc("MBVD"),
        four_cc("MFED"), four_cc("MGI2"), four_cc("MNLD"), four_cc("MDDL"), four_cc("MOMX"),
      };

      /** Serializer hook (see write_entity): stamp the derived MOHD counts into
          the just-written header payload — n_groups from the MOGI table,
          n_portals and n_doodad_sets from their vectors. The counts real client
          files disagree with their containers on (n_textures, n_lights,
          n_doodad_defs, n_doodad_names) stay stored fields; see SMOHeader.
          @param fourcc  the emitted chunk's id, in disk layout.
          @param payload the finished chunk payload, patched in place. */
      [[=welder::mark::exclude]]
      void patch_chunk(std::uint32_t fourcc, std::span<std::byte> payload) const
      {
        if (fourcc != four_cc("MOHD") || payload.size() < sizeof(SMOHeader))
          return;
        SMOHeader h;
        std::memcpy(&h, payload.data(), sizeof h);
        h.n_groups = static_cast<std::uint32_t>(group_infos.size());
        h.n_portals = static_cast<std::uint32_t>(portals.size());
        h.n_doodad_sets = static_cast<std::uint32_t>(doodad_sets.size());
        std::memcpy(payload.data(), &h, sizeof h);
      }
    };
  }

  /** A WMO root file — the canonicalizing face of detail::WMORoot: every
      client version maps to its range's first grid version
      (wmo_root_pivots), so e.g. one instantiation serves Vanilla through
      WoD. */
  template <ClientVersion V>
  using WMORoot =
    detail::WMORoot<canonical_version(V, wmo_root_pivots, wmo_versions)>;

}
