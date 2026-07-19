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
  struct WmoRoot : chunk_extras
  {
    static constexpr ClientVersion version = V;

    [[=chunk("MVER")]] std::uint32_t mver = wmo_version_v17;
    [[=chunk("MOHD")]] SMOHeader header{};

    /** Texture filenames (pre-8.1, or the 8.1+ fallback mode: its presence in
        a file means MOMT texture fields are MOTX offsets, not FileDataIDs). */
    [[=chunk("MOTX"), =formats::optional]] string_block textures;

    [[=chunk("MOMT"), =formats::optional]] std::vector<SMOMaterial> materials;
    [[=chunk("MOGN"), =formats::optional]] string_block group_names;
    [[=chunk("MOGI"), =formats::optional]] std::vector<SMOGroupInfo> group_infos;

    /** Skybox filename; kept raw — files pad it to 4-byte alignment. */
    [[=chunk("MOSB"), =formats::optional]] chunk_blob skybox_name;
    [[=chunk("MOSI"), =since(wmo_fdid_refs), =formats::optional]]
    std::vector<std::uint32_t> skybox_fdid;

    [[=chunk("MOPV"), =formats::optional]] std::vector<C3Vector> portal_vertices;
    [[=chunk("MOPT"), =formats::optional]] std::vector<SMOPortal> portals;
    [[=chunk("MOPR"), =formats::optional]] std::vector<SMOPortalRef> portal_refs;
    [[=chunk("MOVV"), =formats::optional]] std::vector<C3Vector> visible_block_vertices;
    [[=chunk("MOVB"), =formats::optional]] std::vector<SMOVisibleBlock> visible_blocks;
    [[=chunk("MOLT"), =formats::optional]] std::vector<SMOLight> lights;
    [[=chunk("MODS"), =formats::optional]] std::vector<SMODoodadSet> doodad_sets;

    /** Doodad filenames (pre-8.1 or fallback; see textures). */
    [[=chunk("MODN"), =formats::optional]] string_block doodad_names;
    [[=chunk("MODI"), =since(wmo_fdid_refs), =formats::optional]]
    std::vector<std::uint32_t> doodad_fdids;

    [[=chunk("MODD"), =formats::optional]] std::vector<SMODoodadDef> doodad_defs;
    [[=chunk("MFOG"), =formats::optional]] std::vector<SMOFog> fogs;
    [[=chunk("MCVP"), =formats::optional]] std::vector<C4Plane> convex_volume_planes;
    [[=chunk("GFID"), =since(wmo_legion), =formats::optional]]
    std::vector<std::uint32_t> group_fdids;

    // Later-expansion root chunks (MOUV, MDDI, MAVG, MAVD, MBVD, MFED, MGI2,
    // MNLD, MDDL, MPVD, MOLV, MOPE, ...) are not modeled yet: they are
    // preserved verbatim in chunk_extras::unknown and round-trip untouched.
  };

  /** The payload of the MOGP container chunk: the group header prelude
      followed by the geometry subchunks. */
  template <ClientVersion V>
  struct WmoGroupBody : chunk_extras
  {
    static constexpr ClientVersion version = V;

    [[=formats::header]] SMOGroupHeader<V> header{};

    [[=chunk("MOPY"), =formats::optional]] std::vector<SMOPoly> polys;
    [[=chunk("MOVI"), =formats::optional]] std::vector<std::uint16_t> indices;
    [[=chunk("MOVT"), =formats::optional]] std::vector<C3Vector> vertices;
    [[=chunk("MONR"), =formats::optional]] std::vector<C3Vector> normals;

    /** Up to three texture-coordinate sets; count driven by the group flags
        (has_two_motv, has_three_motv). */
    [[=chunk("MOTV"), =formats::optional, =repeats(3)]]
    repeated<std::vector<C2Vector>, 3> texcoords;

    [[=chunk("MOBA"), =formats::optional]] std::vector<SMOBatch<V>> batches;
    [[=chunk("MOLR"), =formats::optional]] std::vector<std::uint16_t> light_refs;
    [[=chunk("MODR"), =formats::optional]] std::vector<std::uint16_t> doodad_refs;
    [[=chunk("MOBN"), =formats::optional]] std::vector<CAaBspNode> bsp_nodes;
    [[=chunk("MOBR"), =formats::optional]] std::vector<std::uint16_t> bsp_face_indices;

    /** Up to two vertex-color layers (has_vertex_colors, has_two_mocv). */
    [[=chunk("MOCV"), =formats::optional, =repeats(2)]]
    repeated<std::vector<CImVector>, 2> vertex_colors;

    /** Liquid data; intra-chunk offset structure, kept opaque for now. */
    [[=chunk("MLIQ"), =formats::optional]] chunk_blob liquid;

    [[=chunk("MORI"), =formats::optional]] std::vector<std::uint16_t> trans_batch_indices;
    [[=chunk("MORB"), =formats::optional]] chunk_blob trans_batch_extra;
    [[=chunk("MOBS"), =formats::optional]] chunk_blob shadow_batches;

    // MOLP/MOLS/MDAL/MOPL/MOPB/MOLM/MOLD/MPBV.../MOGX/MPY2/MOQG and other
    // era-specific subchunks stay in chunk_extras::unknown, round-tripping
    // verbatim.
  };

  /** One group file: MVER plus the MOGP container. */
  template <ClientVersion V>
  struct WmoGroup : chunk_extras
  {
    static constexpr ClientVersion version = V;

    [[=chunk("MVER")]] std::uint32_t mver = wmo_version_v17;
    [[=chunk("MOGP"), =formats::container]] WmoGroupBody<V> body{};
  };

  /** A whole WMO — the root and its group files as one entity.

      Group files are located through GFID when present (Legion+ clients),
      otherwise by the "{root}_{NNN}.wmo" naming convention. */
  template <ClientVersion V>
  struct Wmo
  {
    static constexpr ClientVersion version = V;

    WmoRoot<V> root{};
    std::vector<WmoGroup<V>> groups;

    /** Load a WMO and all its groups from a client filesystem.
        @param fs  the filesystem gateway.
        @param key the root file identity (path and/or FileDataID).
        @return the assembled entity, or the first error. */
    static Result<Wmo> load(fs::FileSystem& fs, const FileKey& key);

    /** Serialize and store the WMO (root and groups) through the filesystem's
        project overlay. The root key must resolve to a path — group files are
        stored under derived "{root}_{NNN}.wmo" names.
        @param fs  the filesystem gateway.
        @param key the root file identity.
        @return nothing, or the first error. */
    Result<void> save(fs::FileSystem& fs, const FileKey& key) const;

    /** Assemble a WMO from already-loaded buffers (no filesystem access).
        @param root_data   the root file bytes.
        @param group_datas one buffer per group file, in group order.
        @return the assembled entity, or the first error. */
    static Result<Wmo> parse(std::span<const std::byte> root_data,
                             std::span<const std::span<const std::byte>> group_datas);

    /** Serialize the root file.
        @return the root file bytes. */
    Result<FileBuffer> write_root() const;

    /** Serialize one group file.
        @param index the group index.
        @return the group file bytes. */
    Result<FileBuffer> write_group(std::size_t index) const;
  };

  extern template struct WmoRoot<versions::wotlk>;
  extern template struct WmoRoot<versions::shadowlands>;
  extern template struct WmoGroupBody<versions::wotlk>;
  extern template struct WmoGroupBody<versions::shadowlands>;
  extern template struct WmoGroup<versions::wotlk>;
  extern template struct WmoGroup<versions::shadowlands>;
  extern template struct Wmo<versions::wotlk>;
  extern template struct Wmo<versions::shadowlands>;
}
