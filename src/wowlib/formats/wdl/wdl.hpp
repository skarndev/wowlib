#pragma once

/** @file
    The WDL entity (namespace wowlib::formats::wdl): a map's low-resolution
    heightmap — the background mountain silhouettes and the minimap fallback.

    A WDL is one chunked file, but its tail is offset-driven: MAOF holds
    64x64 absolute file offsets, one per map tile, each pointing at that
    tile's MARE heightmap chunk, which its MAHO hole mask (and, per era, an
    MAOC/MAOE record) follows. The per-tile chunks are `repeating` members —
    the i-th heightmap belongs to the i-th nonzero MAOF slot in row-major
    order — and two serializer hooks keep the offset machinery honest:
    resequenced_journal() rebuilds the emission order (interleaving each
    tile's chunks) once tiles were added or removed, and patch_file() stamps
    the MAOF offsets into the finished image, so they are always derived,
    never stale. An entity read from a client and left unmodified rewrites
    byte-for-byte. */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/map_placements.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wdl/boundaries.hpp>
#include <wowlib/formats/wdl/chunks/objects.hpp>
#include <wowlib/formats/wdl/chunks/skyscene.hpp>
#include <wowlib/formats/wdl/chunks/tiles.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::wdl
{
  using namespace wowlib::formats::wdl::chunks;

  /** The version-agnostic base of every WDL<V> (welded as "WDL").

      This empty base exists ENTIRELY for the language bindings (Python, Lua):
      it gives the per-version WDL* classes a common welded supertype, and the
      module glue attaches the for_version/read/write/convert surface to it
      (dispatching to the concrete version). It has no role in the C++ API,
      where you use the concrete WDL<V> directly.

      @see https://wowdev.wiki/WDL */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WDL"),
    =welder::doc(R"(
        A map's low-resolution heightmap file, abstract over the client
        version — the background mountain silhouettes. Construct the concrete
        version with WDL.for_version(expansion), then read()/write(); the
        per-version WDL* classes are subclasses. See
        https://wowdev.wiki/WDL.)")
  ]] WDLBase
  {
  };

  namespace detail
  {
    // --- version-range trait bases (unwelded) ---------------------------------
    // One struct per availability range; members keep their chunk/since/until/
    // doc/marks (read off the declaring class, so flattening preserves them).

    /** The pre-Legion per-tile occlusion mesh. */
    struct WdlPreLegion
    {
      [[
        =chunk("MAOC"),
        =until(builds::Legion),
        =formats::optional,
        =formats::repeating,
        =welder::doc(R"(Per-tile occlusion mesh vertices (MAOC, pre-Legion; optional
                        and absent from every surveyed file), one record per
                        occurrence, kept opaque. Follows its tile's heightmap in the
                        stream.)")]]
      std::vector<ChunkBlob> occlusion_meshes;
    };

    /** The TBC+ per-tile hole masks. */
    struct WdlTbc
    {
      [[
        =chunk("MAHO"),
        =since(builds::TBC),
        =formats::optional,
        =formats::repeating,
        =welder::mark::no_reassign,
        =welder::doc(R"(Per-tile hole masks (MAHO, TBC+): the i-th mask belongs to
                        the i-th heightmap. Blizzard writes one per tile even when
                        all zero; hole masks are all-or-nothing — leave the list
                        empty or give every heightmap its mask. (wowdev.wiki dates
                        MAHO to WotLK, but vanilla WDLs carry none and every 2.4.3
                        WDL pairs one MAHO per MARE — so it debuts in TBC.))")]]
      std::vector<TileHoles> holes;
    };

    /** The Legion object swap (low-resolution placements) and ocean masks. */
    struct WdlLegion
    {
      [[
        =chunk("MLDD"),
        =since(builds::Legion),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Low-resolution M2 placements (MLDD, Legion+), drawn instead
                        of the far-away real models; name_id is always a
                        FileDataID here.)")]]
      std::vector<SMDoodadDef> lod_doodads;

      [[
        =chunk("MLDX"),
        =since(builds::Legion),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Visibility extents for the M2 placements (MLDX, Legion+);
                        same count and order as lod_doodads.)")]]
      std::vector<LodExtent> lod_doodad_extents;

      [[
        =chunk("MLMD"),
        =since(builds::Legion),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Low-resolution WMO placements (MLMD, Legion+); sorted by
                        their extent radius, largest first, in shipped files.)")]]
      std::vector<LodMapObjDef> lod_map_objects;

      [[
        =chunk("MLMX"),
        =since(builds::Legion),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Visibility extents for the WMO placements (MLMX, Legion+);
                        same count and order as lod_map_objects.)")]]
      std::vector<LodExtent> lod_map_object_extents;

      [[
        =chunk("MAOE"),
        =since(builds::Legion),
        =formats::optional,
        =formats::repeating,
        =welder::mark::no_reassign,
        =welder::doc(R"(Sparse per-tile ocean masks (MAOE, Legion+), emitted between
                        a tile's heightmap and its hole mask — only SOME tiles have
                        one, so use ocean_mask_tiles() for the mask -> heightmap
                        pairing.)")]]
      std::vector<TileOcean> ocean_masks;
    };

    /** The BfA fade distances and WMO-map byte. */
    struct WdlBfA
    {
      [[
        =chunk("MLDF"),
        =since(builds::BfA),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Fade-distance ranges for the M2 placements (MLDF, BfA);
                        same count and order as lod_doodads. Undocumented on
                        wowdev; the layout and the BfA (not Shadowlands) debut
                        are survey findings across every 8.3.7 WDL carrying the
                        chunk.)")]]
      std::vector<LodDoodadFade> lod_doodad_fades;

      [[
        =chunk("MLMB"),
        =since(builds::BfA),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(One byte per WMO placement (MLMB, BfA; same count and
                        order as lod_map_objects — the ADT twin pairs with
                        MODF in _obj0 and MLMD in _obj1). Semantics unknown;
                        the 8.3.7 fleet survey (1300+ instances) shows an
                        enum-like value set (0x19/0x20/0x26/0x33/0x40/0x46/
                        0x80) that clusters per map, varies per instance of
                        the same asset, and does not correlate with the
                        placement radius; 0x80 co-occurs with other values on
                        the same asset like an override state.)")]]
      std::vector<std::uint8_t> mlmb;
    };

    /** The Shadowlands sky scenes and undocumented blobs. */
    struct WdlSL
    {
      [[
        =chunk("MLDL"),
        =since(builds::SL),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(MLDL (9.x+): per-lod_doodads values, engaged by placement
                        flag 0x8 (as the ADT chunk of the same name).)")]]
      std::vector<std::uint32_t> mldl;

      [[
        =chunk("MLDB"),
        =since(builds::SL),
        =formats::optional,
        =welder::doc("MLDB (9.x+, undocumented); preserved opaque.")]]
      ChunkBlob mldb;

      [[
        =chunk("MSSN"),
        =since(builds::SL),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Sky scenes (MSSN, Shadowlands+).")]]
      std::vector<SkyScene> sky_scenes;

      [[
        =chunk("MSSC"),
        =since(builds::SL),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Sky-scene conditions (MSSC, Shadowlands+), ranged by the "
                     "scenes' condition_index/count.")]]
      std::vector<SkySceneCondition> sky_scene_conditions;

      [[
        =chunk("MSSO"),
        =since(builds::SL),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Sky-scene objects (MSSO, Shadowlands+), ranged by the scenes' "
                     "object_index/count.")]]
      std::vector<SkySceneObject> sky_scene_objects;

      [[
        =chunk("MSSF"),
        =since(builds::SL),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc(R"(Sky-scene object params (MSSF; wowdev dates it Dragonflight+
                        but 9.2.7 files carry it), referenced by the objects'
                        params_index.)")]]
      std::vector<SkySceneObjectParams> sky_scene_object_params;
    };

    /** The War Within scene-living schedule. */
    struct WdlTww
    {
      [[
        =chunk("MSLD"),
        =since(builds::TWW),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Scene-living definitions (MSLD, The War Within+).")]]
      std::vector<SceneLivingDef> scene_living_defs;

      [[
        =chunk("MSLI"),
        =since(builds::TWW),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Scene-living indices (MSLI, The War Within+): one MSLD index "
                     "per sky-scene object.")]]
      std::vector<std::int32_t> scene_living_indices;
    };
  }

  namespace detail
  {
    /** A WDL low-resolution heightmap file for one client version.
        Instantiate through the canonicalizing wdl::WDL alias, never directly.

        The tile chunks pair by ordinal: the i-th heightmap belongs to the
        i-th nonzero tile_offsets slot (row-major), and the i-th hole mask to
        the i-th heightmap. The MAOF offsets themselves are DERIVED — stamped
        from the finished layout on every write — so only the nonzero pattern
        is authored data; toggle a slot nonzero (any value) and give it a
        heightmap to add a tile.

        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WDL */
    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc(R"(
          A map's low-resolution heightmap file for one client version: the
          64x64 tile offset table, one 17x17+16x16 int16 heightmap (and hole
          mask) per present tile, the low-resolution object placements of the
          era, and the Shadowlands+ sky scenes. Tile chunks pair by ordinal —
          the i-th heightmap belongs to the i-th nonzero tile_offsets slot
          (row-major); the offsets themselves are recomputed on every write,
          so only the nonzero pattern is authored data. An instance read from
          a client file rewrites byte-for-byte until modified. See
          https://wowdev.wiki/WDL.)")
    ]] WDL
      : ChunkedFile<WDL<V>>, WDLBase,
        slot<V, ClientVersion{0, 0, 0, 0}, WdlPreLegion, builds::Legion>,
        slot<V, builds::TBC, WdlTbc>,
        slot<V, builds::Legion, WdlLegion>,
        slot<V, builds::BfA, WdlBfA>,
        slot<V, builds::SL, WdlSL>,
        slot<V, builds::TWW, WdlTww>
    {
      static constexpr ClientVersion version = V;

      [[
        =chunk("MVER"),
        =welder::doc("The WDL format version; 18 for every supported client.")]]
      std::uint32_t mver = wdl_version_18;

      [[
        =chunk("MWMO"),
        =formats::optional,
        =welder::doc(R"(WMO silhouette filenames (MWMO): zero-terminated strings,
                        referenced by offset. Every pre-Legion file carries the
                        chunk (often empty); Legion+ terrain maps replace the
                        object set with the MLDD/MLMD placements, but WMO-only
                        maps keep shipping it.)")]]
      StringBlock wmo_filenames;

      [[
        =chunk("MWID"),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("MWMO filename start offsets (MWID), one per name.")]]
      std::vector<std::uint32_t> wmo_filename_offsets;

      [[
        =chunk("MODF"),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("WMO silhouette placements (MODF), one per MWMO name; the same "
                     "64-byte record the WDT and ADT use.")]]
      std::vector<SMMapObjDef> wmo_placements;

      [[
        =chunk("MAOF"),
        =welder::mark::no_reassign,
        =welder::doc(R"(The tile offset table (MAOF): 64 x 64 absolute file offsets
                        in row-major order (y outer, x inner), 0 for absent tiles.
                        The offset VALUES are derived — every write restamps them
                        from the finished layout — so only the nonzero pattern is
                        authored: the i-th nonzero slot owns the i-th heightmap.)")]]
      std::vector<std::uint32_t> tile_offsets;

      [[
        =chunk("MARE"),
        =formats::optional,
        =formats::repeating,
        =welder::mark::no_reassign,
        =welder::doc(R"(The per-tile heightmaps (MARE), one per nonzero tile_offsets
                        slot, in row-major slot order.)")]]
      std::vector<TileHeights> heightmaps;

      /** The canonical chunk-stream order the serializer emits a fresh entity
          in (see write_order); the per-tile interleave is produced by
          resequenced_journal(), not this table. Lists every chunk member
          exactly once. */
      static constexpr std::array chunk_order = {
        four_cc("MVER"), four_cc("MWMO"), four_cc("MWID"), four_cc("MODF"),
        four_cc("MLDD"), four_cc("MLDX"), four_cc("MLDF"), four_cc("MLDL"),
        four_cc("MLDB"), four_cc("MLMD"), four_cc("MLMX"), four_cc("MLMB"),
        four_cc("MSSN"), four_cc("MSSC"), four_cc("MSSO"), four_cc("MSSF"),
        four_cc("MSLD"), four_cc("MSLI"), four_cc("MAOF"), four_cc("MARE"),
        four_cc("MAOC"), four_cc("MAOE"), four_cc("MAHO"),
      };

      /** The heightmap ordinal each ocean mask belongs to (parallel to
          ocean_masks): derived from the stored journal's interleave, or the
          identity pairing for fresh entities. Empty for versions without
          MAOE. */
      [[=welder::doc(R"(The heightmap ordinal each ocean mask belongs to (parallel
                        to ocean_masks), derived from the read file's chunk
                        interleave. Empty when there are no ocean masks.)"),
        =welder::returns("one heightmap ordinal per ocean mask")]]
      std::vector<std::uint32_t> ocean_mask_tiles() const;

      /** Validation hook (see formats::detail::validate_value): the tile-table
          pairing invariants — the MAOF table's shape, and the ordinal pairing
          that makes the i-th nonzero slot own the i-th heightmap. These are
          the SAME contracts the write path must hold to lay out a rebuilt
          journal, so write() checks them through this hook rather than
          restating them (see resequenced_journal).
          @param report the report findings land in. */
      [[=welder::mark::exclude]]
      void validate_extra(ValidationReport& report) const;

      /** Serializer hook (see write_entity): once the stored journal no
          longer matches the tile data (tiles added/removed, or a fresh
          entity), rebuild the emission order — every non-tile chunk in
          canonical order, then each tile's MARE/(MAOC/MAOE)/MAHO interleaved
          after MAOF, preserved unknown chunks last. Requires the tile-table
          pairing invariants (validate_extra) to hold.
          @return nullopt to replay the stored journal, the rebuilt journal
                  otherwise, or the validation error. */
      [[=welder::mark::exclude]]
      Result<std::optional<std::vector<JournalEntry>>> resequenced_journal() const;

      /** Serializer hook (see write_entity): stamp the MAOF table in the
          finished image — the i-th nonzero slot receives the i-th MARE
          chunk's absolute offset.
          @param image this entity's complete serialized image.
          @return nothing, or the pairing-mismatch error. */
      [[=welder::mark::exclude]]
      Result<void> patch_file(std::span<std::byte> image) const;

      [[=welder::mark::only(welder::lang::lua),
        =welder::doc("Load the WDL from a client filesystem, replacing this "
                     "entity's contents.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key
                        [[=welder::doc("the file identity (path and/or FileDataID)")]]);

      [[=welder::mark::only(welder::lang::lua),
        =welder::doc("Serialize and store the WDL through the filesystem's project "
                     "overlay.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key
                         [[=welder::doc("the file identity; must resolve to a path")]]) const;

      // the inherited ChunkedFile read(span)/write() stay available for
      // buffer-level access
      using ChunkedFile<WDL<V>>::read;
      using ChunkedFile<WDL<V>>::write;
    };
  }

  /** A WDL low-resolution heightmap — the canonicalizing face of
      detail::WDL: every client version maps to its range's first grid
      version (wdl_pivots), so five instantiations serve all eleven
      releases. */
  template <ClientVersion V>
  using WDL = detail::WDL<canonical_version(V, wdl_pivots, wdl_versions)>;
}

// --- method definitions -------------------------------------------------------
// Inline in this header: the entity is a template, so the definitions must be
// visible for implicit instantiation — the library ships NO explicit
// instantiations; the bindings expand the full matrix in their own TUs.
namespace wowlib::formats::wdl
{
  template <ClientVersion V>
  std::vector<std::uint32_t> detail::WDL<V>::ocean_mask_tiles() const
  {
    std::vector<std::uint32_t> out;
    if constexpr (requires { this->ocean_masks; })
    {
      constexpr auto mare_idx = formats::detail::chunk_member_index<WDL>(four_cc("MARE"));
      constexpr auto maoe_idx = formats::detail::chunk_member_index<WDL>(four_cc("MAOE"));
      const std::size_t n = this->ocean_masks.size();
      if (n == 0)
        return out;
      out.reserve(n);
      std::size_t mares_seen = 0;
      for (const JournalEntry& entry : this->journal)
      {
        if (entry.member == mare_idx)
          ++mares_seen;
        else if (entry.member == maoe_idx && out.size() < n)
          out.push_back(mares_seen == 0 ? 0 : static_cast<std::uint32_t>(mares_seen - 1));
      }
      if (out.size() != n)
      {
        // fresh entity (or masks added since the read): the identity pairing
        out.clear();
        const std::size_t last =
          heightmaps.empty() ? 0 : heightmaps.size() - 1;
        for (std::size_t i = 0; i < n; ++i)
          out.push_back(static_cast<std::uint32_t>(std::min(i, last)));
      }
    }
    return out;
  }

  template <ClientVersion V>
  void detail::WDL<V>::validate_extra(ValidationReport& report) const
  {
    const std::size_t n_tiles = heightmaps.size();
    std::size_t engaged_slots = 0;
    for (const std::uint32_t offset : tile_offsets)
      engaged_slots += (offset != 0);

    // MAOF is a fixed 64x64 slot table; a zero slot means "no tile here"
    if ((n_tiles != 0 || engaged_slots != 0) && tile_offsets.size() != wdl_tile_slots)
      report.add_error("tile_offsets",
                       std::format("the MAOF table holds {} offsets, not 64*64 — resize "
                                   "tile_offsets to {} (0 = tile absent)",
                                   tile_offsets.size(), wdl_tile_slots));
    if (engaged_slots != n_tiles)
      report.add_error("heightmaps",
                       std::format("{} nonzero tile_offsets slots but {} heightmaps — the "
                                   "i-th nonzero slot owns the i-th heightmap, so the "
                                   "counts must match",
                                   engaged_slots, n_tiles));

    if constexpr (requires { this->holes; })
      if (!this->holes.empty() && this->holes.size() != n_tiles)
        report.add_error("holes",
                         std::format("{} hole masks but {} heightmaps — hole masks are "
                                     "all-or-nothing (empty, or one per heightmap)",
                                     this->holes.size(), n_tiles));
    if constexpr (requires { this->ocean_masks; })
      if (this->ocean_masks.size() > n_tiles)
        report.add_error("ocean_masks", std::format("{} ocean masks but only {} heightmaps",
                                                    this->ocean_masks.size(), n_tiles));
    if constexpr (requires { this->occlusion_meshes; })
      if (this->occlusion_meshes.size() > n_tiles)
        report.add_error("occlusion_meshes",
                         std::format("{} occlusion meshes but only {} heightmaps",
                                     this->occlusion_meshes.size(), n_tiles));
  }

  template <ClientVersion V>
  Result<std::optional<std::vector<JournalEntry>>> detail::WDL<V>::resequenced_journal() const
  {
    constexpr auto mare_idx = formats::detail::chunk_member_index<WDL>(four_cc("MARE"));
    constexpr auto maho_idx = formats::detail::chunk_member_index<WDL>(four_cc("MAHO"));
    constexpr auto maoe_idx = formats::detail::chunk_member_index<WDL>(four_cc("MAOE"));
    constexpr auto maoc_idx = formats::detail::chunk_member_index<WDL>(four_cc("MAOC"));
    constexpr auto maof_idx = formats::detail::chunk_member_index<WDL>(four_cc("MAOF"));

    const auto journal_count = [&](std::int32_t index) -> std::size_t {
      if (index < 0)
        return 0;
      std::size_t n = 0;
      for (const JournalEntry& entry : this->journal)
        n += (entry.member == index);
      return n;
    };

    const std::size_t n_tiles = heightmaps.size();
    std::size_t n_holes = 0;
    std::size_t n_ocean = 0;
    std::size_t n_occlusion = 0;
    if constexpr (requires { this->holes; })
      n_holes = this->holes.size();
    if constexpr (requires { this->ocean_masks; })
      n_ocean = this->ocean_masks.size();
    if constexpr (requires { this->occlusion_meshes; })
      n_occlusion = this->occlusion_meshes.size();

    // the stored journal still matches the tile data: replay it as-is
    if (!this->journal.empty() && journal_count(mare_idx) == n_tiles
        && journal_count(maho_idx) == n_holes && journal_count(maoe_idx) == n_ocean
        && journal_count(maoc_idx) == n_occlusion)
      return std::optional<std::vector<JournalEntry>>{};

    // rebuild path: the layout below only makes sense once the tile-table
    // pairing holds, so require it through the same hook validate() uses
    {
      ValidationReport report;
      validate_extra(report);
      if (auto r = report.to_result(); !r)
        return std::unexpected{r.error()};
    }

    // Per-tile attachment of the sparse chunks (MAOE/MAOC): pair through the
    // stored journal's interleave when it still covers them, else the
    // identity pairing (i-th mask -> i-th tile).
    const auto attach = [&](std::int32_t index, std::size_t count) {
      std::vector<std::vector<std::uint32_t>> per(n_tiles);
      if (n_tiles == 0 || count == 0)
        return per;
      if (journal_count(index) == count)
      {
        std::size_t mares_seen = 0;
        bool paired = true;
        for (const JournalEntry& entry : this->journal)
        {
          if (entry.member == mare_idx)
            ++mares_seen;
          else if (entry.member == index)
          {
            if (mares_seen == 0 || entry.occurrence >= count)
            {
              paired = false;
              break;
            }
            per[std::min(mares_seen - 1, n_tiles - 1)].push_back(entry.occurrence);
          }
        }
        if (paired)
          return per;
        per.assign(n_tiles, {});
      }
      for (std::size_t i = 0; i < count; ++i)
        per[std::min(i, n_tiles - 1)].push_back(static_cast<std::uint32_t>(i));
      return per;
    };
    const auto occlusion_per_tile = attach(maoc_idx, n_occlusion);
    const auto ocean_per_tile = attach(maoe_idx, n_ocean);

    // every non-tile chunk in canonical order, ...
    auto out = formats::detail::fresh_journal(*this);
    std::erase_if(out, [&](const JournalEntry& entry) {
      return entry.member == mare_idx || (maho_idx >= 0 && entry.member == maho_idx)
             || (maoe_idx >= 0 && entry.member == maoe_idx)
             || (maoc_idx >= 0 && entry.member == maoc_idx);
    });

    // ... then the interleaved tile block right after MAOF, ...
    std::size_t insert_at = out.size();
    for (std::size_t i = 0; i < out.size(); ++i)
      if (out[i].member == maof_idx)
      {
        insert_at = i + 1;
        break;
      }
    std::vector<JournalEntry> block;
    for (std::size_t tile = 0; tile < n_tiles; ++tile)
    {
      block.push_back({four_cc("MARE"), mare_idx, static_cast<std::uint32_t>(tile)});
      for (const std::uint32_t occurrence : occlusion_per_tile[tile])
        block.push_back({four_cc("MAOC"), maoc_idx, occurrence});
      for (const std::uint32_t occurrence : ocean_per_tile[tile])
        block.push_back({four_cc("MAOE"), maoe_idx, occurrence});
      if (tile < n_holes)
        block.push_back({four_cc("MAHO"), maho_idx, static_cast<std::uint32_t>(tile)});
    }
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(insert_at), block.begin(), block.end());

    // ... and the preserved unknown chunks last, in their stored order
    for (const JournalEntry& entry : this->journal)
      if (entry.member < 0)
        out.push_back(entry);
    return std::optional{std::move(out)};
  }

  template <ClientVersion V>
  Result<void> detail::WDL<V>::patch_file(std::span<std::byte> image) const
  {
    constexpr std::uint32_t maof_cc = four_cc("MAOF");
    constexpr std::uint32_t mare_cc = four_cc("MARE");

    std::size_t maof_payload = image.size();
    std::uint32_t maof_size = 0;
    std::vector<std::uint32_t> mare_offsets;
    std::size_t pos = 0;
    while (image.size() - pos >= 2 * sizeof(std::uint32_t))
    {
      std::uint32_t fourcc = 0;
      std::uint32_t size = 0;
      std::memcpy(&fourcc, image.data() + pos, sizeof fourcc);
      std::memcpy(&size, image.data() + pos + sizeof fourcc, sizeof size);
      if (size > image.size() - pos - 2 * sizeof(std::uint32_t))
        break;  // the preserved trailing-bytes region
      if (fourcc == maof_cc && maof_payload == image.size())
      {
        maof_payload = pos + 2 * sizeof(std::uint32_t);
        maof_size = size;
      }
      else if (fourcc == mare_cc)
        mare_offsets.push_back(static_cast<std::uint32_t>(pos));
      pos += 2 * sizeof(std::uint32_t) + size;
    }

    if (mare_offsets.empty() && maof_size == 0)
      return {};  // no tiles and no table: nothing to stamp
    if (maof_payload == image.size())
      return make_error(ErrorCode::InvalidEntityState,
                        "heightmap chunks without a MAOF table to point at them");
    if (maof_size != wdl_tile_slots * sizeof(std::uint32_t))
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("the written MAOF table holds {} bytes, not 64*64 "
                                    "offsets — resize tile_offsets to {}",
                                    maof_size, wdl_tile_slots));

    std::size_t next = 0;
    for (std::size_t slot = 0; slot < wdl_tile_slots; ++slot)
    {
      std::uint32_t value = 0;
      std::byte* at = image.data() + maof_payload + slot * sizeof value;
      std::memcpy(&value, at, sizeof value);
      if (value == 0)
        continue;
      if (next >= mare_offsets.size())
        return make_error(ErrorCode::InvalidEntityState,
                          std::format("more nonzero tile_offsets slots than the {} written "
                                      "heightmaps",
                                      mare_offsets.size()));
      std::memcpy(at, &mare_offsets[next++], sizeof value);
    }
    if (next != mare_offsets.size())
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("{} written heightmaps but only {} nonzero tile_offsets "
                                    "slots",
                                    mare_offsets.size(), next));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WDL<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto data = fs.read_file(key);
    if (!data)
      return std::unexpected{data.error()};
    *this = WDL{};
    if (auto r = ChunkedFile<WDL>::read(*data); !r)
      return std::unexpected{r.error()};
    if (mver != wdl_version_18)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("WDL MVER is {}, expected {}", mver, wdl_version_18));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WDL<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "saving a WDL needs a path for the file key");
    const auto data = ChunkedFile<WDL>::write();
    if (!data)
      return std::unexpected{data.error()};
    if (auto r = fs.add_file(*resolved.path, *data); !r)
      return std::unexpected{r.error()};
    return {};
  }
}
