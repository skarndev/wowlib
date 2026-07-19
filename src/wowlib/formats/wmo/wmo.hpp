#pragma once

/** @file
    The WMO entity: a v17 world map object with its root file and all group
    files unified, versioned on the client it is laid out for. Reading is
    chunk-order independent; writing replays the original chunk order so an
    untouched entity rewrites byte-for-byte. */

#include <array>
#include <span>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/chunk/serializer.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wmo/wmo_wire.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::formats::wmo
{
  /** GFID and the large-material batch layout arrived with Legion 7.0. */
  inline constexpr ClientVersion wmo_legion = wmo_batch_large_material;

  /** The versions Wmo is instantiated (and welded) for. */
  inline constexpr std::array wmo_versions{versions::wotlk, versions::shadowlands};

  /** The WMO version every supported client uses (MVER payload). */
  inline constexpr std::uint32_t wmo_version_v17 = 17;

  /** The root file: materials, group metadata, doodad placements, portals,
      lights, fog. Members are declared in the canonical client chunk order.

      Only MVER and MOHD are required — real files omit more chunks than the
      documentation admits, and unmodeled or version-foreign chunks round-trip
      through the unknown list untouched. */
  template <ClientVersion V>
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A WMO root file: header, materials, group metadata, portals, lights,
        doodad sets and placements, fog. An instance read from a client file
        rewrites byte-for-byte until modified.)")
  ]]
  WmoRoot : chunk_extras
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
    string_block textures;

    [[=chunk("MOMT"), =formats::optional, =welder::doc("Materials (MOMT).")]]
    std::vector<SMOMaterial> materials;

    [[=chunk("MOGN"), =formats::optional,
      =welder::doc("Group names (MOGN), referenced by byte offset.")]]
    string_block group_names;

    [[=chunk("MOGI"), =formats::optional, =welder::doc("Per-group info (MOGI).")]]
    std::vector<SMOGroupInfo> group_infos;

    [[=chunk("MOSB"), =formats::optional,
      =welder::doc("Skybox filename (MOSB); raw bytes - files pad it to 4-byte "
                   "alignment.")]]
    chunk_blob skybox_name;

    [[=chunk("MOSI"), =since(wmo_fdid_refs), =formats::optional,
      =welder::doc("Skybox FileDataID (MOSI, 8.1+).")]]
    std::vector<std::uint32_t> skybox_fdid;

    [[=chunk("MOPV"), =formats::optional, =welder::doc("Portal vertices (MOPV).")]]
    std::vector<C3Vector> portal_vertices;

    [[=chunk("MOPT"), =formats::optional, =welder::doc("Portals (MOPT).")]]
    std::vector<SMOPortal> portals;

    [[=chunk("MOPR"), =formats::optional,
      =welder::doc("Portal references from groups (MOPR).")]]
    std::vector<SMOPortalRef> portal_refs;

    [[=chunk("MOVV"), =formats::optional,
      =welder::doc("Visible block vertices (MOVV).")]]
    std::vector<C3Vector> visible_block_vertices;

    [[=chunk("MOVB"), =formats::optional, =welder::doc("Visible blocks (MOVB).")]]
    std::vector<SMOVisibleBlock> visible_blocks;

    [[=chunk("MOLT"), =formats::optional, =welder::doc("Lights (MOLT).")]]
    std::vector<SMOLight> lights;

    [[=chunk("MODS"), =formats::optional, =welder::doc("Doodad sets (MODS).")]]
    std::vector<SMODoodadSet> doodad_sets;

    [[=chunk("MODN"), =formats::optional,
      =welder::doc("Doodad (M2) filenames (MODN; pre-8.1 or fallback, see "
                   "textures).")]]
    string_block doodad_names;

    [[=chunk("MODI"), =since(wmo_fdid_refs), =formats::optional,
      =welder::doc("Doodad FileDataIDs (MODI, 8.1+; replaces doodad_names).")]]
    std::vector<std::uint32_t> doodad_fdids;

    [[=chunk("MODD"), =formats::optional,
      =welder::doc("Doodad placements (MODD).")]]
    std::vector<SMODoodadDef> doodad_defs;

    [[=chunk("MFOG"), =formats::optional, =welder::doc("Fog volumes (MFOG).")]]
    std::vector<SMOFog> fogs;

    [[=chunk("MCVP"), =formats::optional,
      =welder::doc("Convex volume planes (MCVP); transports mostly.")]]
    std::vector<C4Plane> convex_volume_planes;

    [[=chunk("GFID"), =since(wmo_legion), =formats::optional,
      =welder::doc("Group file FileDataIDs (GFID, Legion+), in group order.")]]
    std::vector<std::uint32_t> group_fdids;

    // Later-expansion root chunks (MOUV, MDDI, MAVG, MAVD, MBVD, MFED, MGI2,
    // MNLD, MDDL, MPVD, MOLV, MOPE, ...) are not modeled yet: they are
    // preserved verbatim in chunk_extras::unknown and round-trip untouched.
  };

  /** The payload of the MOGP container chunk: the group header prelude
      followed by the geometry subchunks. */
  template <ClientVersion V>
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The MOGP payload: the group header and the geometry subchunks "
                 "(triangles, vertices, normals, batches, BSP, liquid).")
  ]]
  WmoGroupBody : chunk_extras
  {
    static constexpr ClientVersion version = V;

    [[=formats::header, =welder::doc("The group header leading the MOGP payload.")]]
    SMOGroupHeader<V> header{};

    [[=chunk("MOPY"), =formats::optional,
      =welder::doc("Per-triangle material info (MOPY).")]]
    std::vector<SMOPoly> polys;

    [[=chunk("MOVI"), =formats::optional,
      =welder::doc("Triangle vertex indices (MOVI), three per triangle.")]]
    std::vector<std::uint16_t> indices;

    [[=chunk("MOVT"), =formats::optional, =welder::doc("Vertices (MOVT).")]]
    std::vector<C3Vector> vertices;

    [[=chunk("MONR"), =formats::optional, =welder::doc("Normals (MONR).")]]
    std::vector<C3Vector> normals;

    /** Up to three texture-coordinate sets; count driven by the group flags
        (has_two_motv, has_three_motv). Excluded from bindings until repeated<>
        grows a sequence protocol. */
    [[=chunk("MOTV"), =formats::optional, =repeats(3), =welder::mark::exclude]]
    repeated<std::vector<C2Vector>, 3> texcoords;

    [[=chunk("MOBA"), =formats::optional, =welder::doc("Render batches (MOBA).")]]
    std::vector<SMOBatch<V>> batches;

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
    repeated<std::vector<CImVector>, 2> vertex_colors;

    [[=chunk("MLIQ"), =formats::optional,
      =welder::doc("Liquid data (MLIQ); intra-chunk offset structure, kept "
                   "opaque for now.")]]
    chunk_blob liquid;

    [[=chunk("MORI"), =formats::optional,
      =welder::doc("Transition batch indices (MORI).")]]
    std::vector<std::uint16_t> trans_batch_indices;

    [[=chunk("MORB"), =formats::optional,
      =welder::doc("Transition batch extra data (MORB), unparsed.")]]
    chunk_blob trans_batch_extra;

    [[=chunk("MOBS"), =formats::optional,
      =welder::doc("Shadow batches (MOBS, Cataclysm+), unparsed.")]]
    chunk_blob shadow_batches;

    // MOLP/MOLS/MDAL/MOPL/MOPB/MOLM/MOLD/MPBV.../MOGX/MPY2/MOQG and other
    // era-specific subchunks stay in chunk_extras::unknown, round-tripping
    // verbatim.
  };

  /** One group file: MVER plus the MOGP container. */
  template <ClientVersion V>
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One WMO group file: the format version and the MOGP container "
                 "holding the geometry.")
  ]]
  WmoGroup : chunk_extras
  {
    static constexpr ClientVersion version = V;

    [[=chunk("MVER"),
      =welder::doc("The WMO format version; 17 for every supported client.")]]
    std::uint32_t mver = wmo_version_v17;

    [[=chunk("MOGP"), =formats::container,
      =welder::doc("The MOGP container: group header and geometry.")]]
    WmoGroupBody<V> body{};
  };

  /** A whole WMO — the root and its group files as one entity.

      Group files are located through GFID when present (Legion+ clients),
      otherwise by the "{root}_{NNN}.wmo" naming convention. */
  template <ClientVersion V>
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A whole world map object: the root file and all its group files as one
        entity. Load through a FileSystem; an unmodified entity saves back
        byte-for-byte.)")
  ]]
  Wmo
  {
    static constexpr ClientVersion version = V;

    [[=welder::doc("The root file contents.")]]
    WmoRoot<V> root{};

    [[=welder::doc("The group files, in group order.")]]
    std::vector<WmoGroup<V>> groups;

    /** Load a WMO and all its groups from a client filesystem.
        @param fs  the filesystem gateway.
        @param key the root file identity (path and/or FileDataID).
        @return the assembled entity, or the first error. */
    [[=welder::doc("Load a WMO and all its group files."),
      =welder::returns("the assembled WMO")]]
    static Result<Wmo> load(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                            const FileKey& key
                            [[=welder::doc("the root file identity (path and/or FileDataID)")]]);

    /** Serialize and store the WMO (root and groups) through the filesystem's
        project overlay. The root key must resolve to a path — group files are
        stored under derived "{root}_{NNN}.wmo" names.
        @param fs  the filesystem gateway.
        @param key the root file identity.
        @return nothing, or the first error. */
    [[=welder::doc("Serialize and store the WMO (root and groups) through the "
                   "filesystem's project overlay.")]]
    Result<void> save(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key
                      [[=welder::doc("the root file identity; must resolve to a path")]]) const;

    /** Assemble a WMO from already-loaded buffers (no filesystem access).
        Not welded: span-of-spans does not cross the binding boundary.
        @param root_data   the root file bytes.
        @param group_datas one buffer per group file, in group order.
        @return the assembled entity, or the first error. */
    [[=welder::mark::exclude]]
    static Result<Wmo> parse(std::span<const std::byte> root_data,
                             std::span<const std::span<const std::byte>> group_datas);

    /** Serialize the root file.
        @return the root file bytes. */
    [[=welder::doc("Serialize the root file."),
      =welder::returns("the root file bytes")]]
    Result<FileBuffer> write_root() const;

    /** Serialize one group file.
        @param index the group index.
        @return the group file bytes. */
    [[=welder::doc("Serialize one group file."),
      =welder::returns("the group file bytes")]]
    Result<FileBuffer> write_group(std::size_t index
                                   [[=welder::doc("the group index")]]) const;
  };

}

namespace wowlib::formats
{
  template <>
  inline constexpr auto supported_versions<wmo::Wmo> = wmo::wmo_versions;

  // The bindings surface for the versioned templates: welder welds a
  // class-template instantiation through a namespace-scope alias, whose
  // identifier is the target-language name (flat suffixed classes, e.g.
  // wowlib.formats.WmoWotlk). Keep in sync with wmo::wmo_versions; declared in
  // dependency order (referenced types before the types whose members name
  // them). Version-invariant wire structs weld under their own names in the
  // wmo submodule instead.
  using WmoGroupHeaderWotlk = wmo::SMOGroupHeader<versions::wotlk>;
  using WmoGroupHeaderShadowlands = wmo::SMOGroupHeader<versions::shadowlands>;
  using WmoBatchWotlk = wmo::SMOBatch<versions::wotlk>;
  using WmoBatchShadowlands = wmo::SMOBatch<versions::shadowlands>;
  using WmoRootWotlk = wmo::WmoRoot<versions::wotlk>;
  using WmoRootShadowlands = wmo::WmoRoot<versions::shadowlands>;
  using WmoGroupBodyWotlk = wmo::WmoGroupBody<versions::wotlk>;
  using WmoGroupBodyShadowlands = wmo::WmoGroupBody<versions::shadowlands>;
  using WmoGroupWotlk = wmo::WmoGroup<versions::wotlk>;
  using WmoGroupShadowlands = wmo::WmoGroup<versions::shadowlands>;
  using WmoWotlk = wmo::Wmo<versions::wotlk>;
  using WmoShadowlands = wmo::Wmo<versions::shadowlands>;
}

namespace wowlib::formats::wmo
{
  extern template struct WmoRoot<versions::wotlk>;
  extern template struct WmoRoot<versions::shadowlands>;
  extern template struct WmoGroupBody<versions::wotlk>;
  extern template struct WmoGroupBody<versions::shadowlands>;
  extern template struct WmoGroup<versions::wotlk>;
  extern template struct WmoGroup<versions::shadowlands>;
  extern template struct Wmo<versions::wotlk>;
  extern template struct Wmo<versions::shadowlands>;
}
