#pragma once

/** @file
    The ADT terrain-tile entity (namespace wowlib::formats::adt): ADT<V>, one map
    tile, unified across the physical files it is stored in.

    A tile is 16x16 = 256 terrain cells (MapChunk) plus tile-wide texture, model
    and placement tables. Pre-Cataclysm it is a single .adt; Cataclysm split it
    into a root .adt and _tex0/_obj0/_obj1(/_lod) satellites, DISTRIBUTING the
    same chunks (and each cell's MCNK sub-chunks) across them. wowlib models ONE
    ADT<V> holding everything: the reader loads every file of the tile and merges
    them, the writer re-distributes on save — so user code adds a texture or a
    doodad without caring which file it lands in. Version-gated tile chunks live
    in conditionally-inherited trait bases (adt::detail).

    ADT does not guarantee a byte-identical round-trip (alpha maps are re-encoded,
    the MHDR/MCIN/MCNK offset tables are re-derived): the contract is a semantic
    round-trip, parse(write(x)) == parse(x). */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/adt/boundaries.hpp>
#include <wowlib/formats/adt/chunks/header.hpp>
#include <wowlib/formats/adt/chunks/texture.hpp>
#include <wowlib/formats/adt/liquid.hpp>
#include <wowlib/formats/adt/map_chunk.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/map_placements.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/version_range.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::adt
{
  using namespace wowlib::formats::adt::chunks;

  /** The version-agnostic base of every ADT<V> (welded as "ADT"): the language
      bindings attach for_version/read/write/convert here. No role in the C++ API,
      where you use the concrete ADT<V> directly. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("ADT"),
    =welder::doc(R"(
        A terrain map tile, abstract over the client version — the .adt file (and,
        since Cataclysm, its _tex0/_obj0/_obj1/_lod satellites) as one entity.
        Construct the concrete version with ADT.for_version(expansion), then
        read()/write(); the per-version ADT* classes are subclasses. See
        https://wowdev.wiki/ADT/v18.)")
  ]] ADTBase
  {
  };

  namespace detail
  {
    /** The flying bounds (MFBO), BC+. Presence follows the MHDR has_mfbo flag. */
    struct ADTFlying
    {
      [[=welder::doc("The flying bounds (MFBO, BC+); engaged by the header has_mfbo flag.")]]
      MFBOPlanes flying_bounds{};
    };

    /** WotLK+ tile chunks: the water (MH2O) and the texture flags (MTXF). */
    struct ADTWotlk
    {
      [[=welder::doc("The tile's water (MH2O, WotLK+): one liquid entry per cell.")]]
      MH2OData water{};

      [[=welder::doc("Per-texture flags (MTXF, WotLK+): one entry per MTEX texture."),
        =welder::mark::no_reassign]]
      std::vector<SMTextureFlags> texture_flags;
    };

    /** Cataclysm+ split-file tile chunks (the tile now spans root/_tex0/_obj0/
        _obj1/_lod files). MTXP is strictly MoP+ but harmless empty on Cata; the
        _obj1 and _lod files are preserved verbatim this stage (structured in a
        later one). */
    struct ADTSplit
    {
      [[=welder::doc("The MAMP alpha-map downscale value (Cata+): overrides the MHDR "
                     "inline value; alpha texture size is 64 / (2^value).")]]
      std::uint32_t mamp = 0;

      [[=welder::doc("Whether this tile stores its textures as MDID/MHID FileDataIDs "
                     "(8.1+ height-texturing maps) rather than MTEX names; set from the "
                     "chunk present on read and honored on write.")]]
      bool uses_texture_fdids = false;

      [[=welder::doc("Height-blend texture parameters (MTXP, MoP+): one per texture."),
        =welder::mark::no_reassign]]
      std::vector<SMTextureParams> texture_params;

      [[=welder::mark::exclude]] std::vector<std::byte> obj1_data;  // raw _obj1.adt
      [[=welder::mark::exclude]] std::vector<std::byte> lod_data;   // raw _lod.adt

      [[=welder::mark::exclude]]

      bool operator==(const ADTSplit&) const = default;
    };

    /** The 8.1+ FileDataID texture tables (_tex0), which replace MTEX names on
        height-texturing maps. */
    struct ADTTexFdids
    {
      [[=welder::doc("Diffuse-texture FileDataIDs (MDID, 8.1+): the _s.blp tileset "
                     "textures MapChunk layers index, in place of MTEX names."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> diffuse_texture_ids;

      [[=welder::doc("Height-texture FileDataIDs (MHID, 8.1+): the _h.blp map paired with "
                     "each diffuse texture (0 for none)."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> height_texture_ids;

      [[=welder::mark::exclude]]

      bool operator==(const ADTTexFdids&) const = default;
    };
  }

  namespace detail
  {
    /** A terrain tile for one client version. Instantiate through the
        canonicalizing adt::ADT alias, never directly. */
    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc(R"(
          A terrain map tile for one client version: the 256 terrain cells plus the
          tile-wide texture, model and placement tables, unified across the physical
          files the tile is stored in. Adding a texture, model or placement does not
          depend on which file it lands in — the writer routes it. An instance read
          from a client and left unmodified rewrites to a semantically equal tile
          (alpha maps and offset tables are re-derived, not byte-preserved). See
          https://wowdev.wiki/ADT/v18.)")
    ]] ADT
      : ADTBase,
        slot<V, builds::TBC, ADTFlying>,
        slot<V, builds::WotLK, ADTWotlk>,
        slot<V, builds::Cata, ADTSplit>,
        slot<V, builds::BfA_TidesOfVengeance, ADTTexFdids>
    {
      static constexpr ClientVersion version = V;

      [[=welder::doc("The ADT format version (MVER); 18 for every supported client.")]]
      std::uint32_t mver = adt_version_18;

      [[=welder::doc("The tile header (MHDR): flags; the chunk offsets are derived.")]]
      MHDRData header{};

      [[=welder::doc("The tileset texture filenames (MTEX): the paths MapChunk layers "
                     "index. Present unless the tile uses MDID/MHID FileDataIDs (8.1+ "
                     "height-texturing maps)."),
        =welder::mark::no_reassign]]
      StringBlock textures;

      [[=welder::doc("The M2 model filenames (MMDX) placements reference by MMID index."),
        =welder::mark::no_reassign]]
      StringBlock model_filenames;

      [[=welder::doc("Byte offsets into model_filenames (MMID): a doodad placement's "
                     "name_id indexes this list."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> model_name_offsets;

      [[=welder::doc("The WMO filenames (MWMO) placements reference by MWID index."),
        =welder::mark::no_reassign]]
      StringBlock wmo_filenames;

      [[=welder::doc("Byte offsets into wmo_filenames (MWID): a WMO placement's name_id "
                     "indexes this list."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> wmo_name_offsets;

      [[=welder::doc("Doodad (M2) placements on this tile (MDDF)."),
        =welder::mark::no_reassign]]
      std::vector<common::SMDoodadDef> doodad_placements;

      [[=welder::doc("WMO placements on this tile (MODF)."),
        =welder::mark::no_reassign]]
      std::vector<common::SMMapObjDef> wmo_placements;

      [[=welder::doc("The 256 terrain cells (MCNK), row-major (index = y * 16 + x)."),
        =welder::mark::no_reassign]]
      std::vector<adt::MapChunk<V>> cells;

      [[=welder::doc("How this tile's alpha maps are laid out on disk (from the map's "
                     "WDT); wowlib always presents decoded 64x64 maps.")]]
      AlphaFormat alpha_format = AlphaFormat::lowres_4bit;

      // --- fs I/O (definitions at the bottom of this header) ------------------

      [[=welder::mark::only(welder::lang::lua),
        =welder::doc("Load the tile — every physical file present — from a client "
                     "filesystem, replacing this entity's contents.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key
                        [[=welder::doc("the tile identity (root .adt path and/or "
                                       "FileDataID)")]]);

      [[=welder::mark::only(welder::lang::lua),
        =welder::doc("Serialize the tile (and, Cata+, every satellite file) through the "
                     "filesystem's project overlay; the file names derive from the key, "
                     "which must resolve to a path.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key
                         [[=welder::doc("the tile identity; must resolve to a path")]]) const;

      /** Parse one physical file's chunk stream into this entity (merging).
          @param data the file bytes.
          @param kind which physical file it is.
          @return a structural error or success. */
      [[=welder::mark::exclude]]
      Result<void> parse_file(std::span<const std::byte> data, FileKind kind);

      /** Serialize the monolithic (pre-Cata) file. */
      [[=welder::mark::exclude]]
      Result<FileBuffer> write_monolithic() const;

      /** Serialize one physical file of a Cata+ split tile (root/_tex0/_obj0). */
      [[=welder::mark::exclude]]
      Result<FileBuffer> write_split_file(FileKind kind) const;

    private:
      /** Normalize every cell's do_not_fix_alpha flag to set (we hold full 64x64
          maps): called once after all of a tile's files are read. */
      void normalize_cells();
    };
  }

  /** A terrain tile — the canonicalizing face of detail::ADT. */
  template <ClientVersion V>
  using ADT = detail::ADT<canonical_version(V, adt_pivots, adt_versions)>;

  namespace detail
  {
    /** Resolve a tile's alpha-map bit depth from its map's WDT MPHD flags. A
        light raw scan (no WDT entity) so ADT does not couple to a WDT version:
        find MPHD, read its flags, test the big-alpha bits (0x4 | 0x80).
        @param fs   the filesystem.
        @param key  the tile key (its path names the map directory).
        @return the resolved format, or lowres_4bit when the WDT is unavailable. */
    inline AlphaFormat resolve_alpha_format(fs::FileSystem& fs, const FileKey& key)
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return AlphaFormat::lowres_4bit;
      std::string_view path = *resolved.path;
      // "world/maps/azeroth/azeroth_32_48.adt" -> "world/maps/azeroth/azeroth.wdt"
      const auto slash = path.find_last_of("/\\");
      const auto dir = slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
      auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
      // strip "_x_y.adt"
      auto us = name.find_last_of('_');
      if (us != std::string_view::npos)
        us = name.substr(0, us).find_last_of('_');
      const auto map = us == std::string_view::npos ? name : name.substr(0, us);
      const std::string wdt_path = std::format("{}/{}.wdt", dir, map);

      const FileKey wdt_key{wdt_path};
      if (!fs.exists(wdt_key))
        return AlphaFormat::lowres_4bit;
      const auto data = fs.read_file(wdt_key);
      if (!data)
        return AlphaFormat::lowres_4bit;
      const auto& bytes = *data;
      std::size_t pos = 0;
      while (pos + 8 <= bytes.size())
      {
        std::uint32_t magic = 0, size = 0;
        std::memcpy(&magic, bytes.data() + pos, 4);
        std::memcpy(&size, bytes.data() + pos + 4, 4);
        if (magic == four_cc("MPHD") && pos + 8 + 4 <= bytes.size())
        {
          std::uint32_t flags = 0;
          std::memcpy(&flags, bytes.data() + pos + 8, 4);
          return (flags & 0x4) || (flags & 0x80) ? AlphaFormat::highres_8bit
                                                 : AlphaFormat::lowres_4bit;
        }
        pos += 8 + size;
      }
      return AlphaFormat::lowres_4bit;
    }

    template <ClientVersion V>
    void ADT<V>::normalize_cells()
    {
      for (auto& cell : cells)
        cell.header.flags |=
          static_cast<std::uint32_t>(MapChunkFlags::do_not_fix_alpha_map);
    }

    template <ClientVersion V>
    Result<void> ADT<V>::parse_file(std::span<const std::byte> data, FileKind kind)
    {
      std::size_t pos = 0;
      std::size_t cell_index = 0;
      while (pos + 8 <= data.size())
      {
        std::uint32_t magic = 0, size = 0;
        std::memcpy(&magic, data.data() + pos, 4);
        std::memcpy(&size, data.data() + pos + 4, 4);
        if (size > data.size() - pos - 8)
          return make_error(ErrorCode::ChunkTruncated,
                            std::format("ADT chunk {} at {:#x} overruns the file",
                                        fourcc_to_string(magic), pos));
        const auto payload = data.subspan(pos + 8, size);
        pos += 8 + size;

        if (magic == four_cc("MVER"))
          std::memcpy(&mver, payload.data(), std::min<std::size_t>(4, payload.size()));
        else if (magic == four_cc("MHDR"))
        {
          std::memcpy(&header, payload.data(), std::min(sizeof(MHDRData), payload.size()));
          // the eleven chunk offsets are derived; zero them so the semantic
          // round-trip does not compare layout artifacts (flags/mamp stay).
          header.ofs_mcin = header.ofs_mtex = header.ofs_mmdx = header.ofs_mmid = 0;
          header.ofs_mwmo = header.ofs_mwid = header.ofs_mddf = header.ofs_modf = 0;
          header.ofs_mfbo = header.ofs_mh2o = header.ofs_mtxf = 0;
        }
        else if (magic == four_cc("MCIN"))
          ;  // derived on write from the MCNK layout; nothing to keep
        else if (magic == four_cc("MTEX"))
        {
          if (auto r = textures.read(payload); !r)
            return r;
        }
        else if (magic == four_cc("MDID"))
        {
          if constexpr (requires { this->diffuse_texture_ids; })
          {
            this->diffuse_texture_ids.resize(payload.size() / 4);
            std::memcpy(this->diffuse_texture_ids.data(), payload.data(),
                        this->diffuse_texture_ids.size() * 4);
            if constexpr (requires { this->uses_texture_fdids; })
              this->uses_texture_fdids = true;
          }
        }
        else if (magic == four_cc("MHID"))
        {
          if constexpr (requires { this->height_texture_ids; })
          {
            this->height_texture_ids.resize(payload.size() / 4);
            std::memcpy(this->height_texture_ids.data(), payload.data(),
                        this->height_texture_ids.size() * 4);
          }
        }
        else if (magic == four_cc("MAMP"))
        {
          if constexpr (requires { this->mamp; })
            std::memcpy(&this->mamp, payload.data(), std::min<std::size_t>(4, payload.size()));
        }
        else if (magic == four_cc("MTXP"))
        {
          if constexpr (requires { this->texture_params; })
          {
            this->texture_params.resize(payload.size() / sizeof(SMTextureParams));
            std::memcpy(this->texture_params.data(), payload.data(),
                        this->texture_params.size() * sizeof(SMTextureParams));
          }
        }
        else if (magic == four_cc("MMDX"))
        {
          if (auto r = model_filenames.read(payload); !r)
            return r;
        }
        else if (magic == four_cc("MMID"))
        {
          model_name_offsets.resize(payload.size() / 4);
          std::memcpy(model_name_offsets.data(), payload.data(), model_name_offsets.size() * 4);
        }
        else if (magic == four_cc("MWMO"))
        {
          if (auto r = wmo_filenames.read(payload); !r)
            return r;
        }
        else if (magic == four_cc("MWID"))
        {
          wmo_name_offsets.resize(payload.size() / 4);
          std::memcpy(wmo_name_offsets.data(), payload.data(), wmo_name_offsets.size() * 4);
        }
        else if (magic == four_cc("MDDF"))
        {
          doodad_placements.resize(payload.size() / sizeof(common::SMDoodadDef));
          std::memcpy(doodad_placements.data(), payload.data(),
                      doodad_placements.size() * sizeof(common::SMDoodadDef));
        }
        else if (magic == four_cc("MODF"))
        {
          wmo_placements.resize(payload.size() / sizeof(common::SMMapObjDef));
          std::memcpy(wmo_placements.data(), payload.data(),
                      wmo_placements.size() * sizeof(common::SMMapObjDef));
        }
        else if (magic == four_cc("MH2O"))
        {
          if constexpr (requires { this->water; })
            if (auto r = this->water.read(payload); !r)
              return r;
        }
        else if (magic == four_cc("MTXF"))
        {
          if constexpr (requires { this->texture_flags; })
          {
            this->texture_flags.resize(payload.size() / sizeof(SMTextureFlags));
            std::memcpy(this->texture_flags.data(), payload.data(),
                        this->texture_flags.size() * sizeof(SMTextureFlags));
          }
        }
        else if (magic == four_cc("MFBO"))
        {
          if constexpr (requires { this->flying_bounds; })
          {
            std::memcpy(&this->flying_bounds, payload.data(),
                        std::min(sizeof(MFBOPlanes), payload.size()));
            header.flags |= static_cast<std::uint32_t>(MapHeaderFlags::has_mfbo);
          }
        }
        else if (magic == four_cc("MCNK"))
        {
          if (cells.size() < cell_index + 1)
            cells.resize(cell_index + 1);
          if (auto r = cells[cell_index].read_slice(payload, kind, alpha_format); !r)
            return r;
          ++cell_index;
        }
        // unmodeled tile chunks (MAMP, MDID/MHID, blend meshes, lod) are handled
        // in later stages; ignored here.
      }
      return {};
    }

    template <ClientVersion V>
    Result<FileBuffer> ADT<V>::write_monolithic() const
    {
      FileBuffer out;
      const auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
      };
      // emit a chunk, returning the fourcc position; body() appends the payload.
      const auto emit = [&](const char (&cc)[5], auto&& body) -> std::size_t {
        const std::size_t at = out.size();
        std::uint32_t magic = four_cc(cc);
        put(&magic, 4);
        const std::size_t size_at = out.size();
        out.insert(out.end(), 4, std::byte{0});
        body();
        const auto size = static_cast<std::uint32_t>(out.size() - size_at - 4);
        std::memcpy(out.data() + size_at, &size, 4);
        return at;
      };

      emit("MVER", [&] { std::uint32_t v = adt_version_18; put(&v, 4); });
      const std::size_t mhdr_at = emit("MHDR", [&] { put(&header, sizeof(MHDRData)); });
      const std::size_t mcin_at = emit("MCIN", [&] { out.insert(out.end(), 256 * 16, std::byte{0}); });

      std::size_t mtex_at = 0, mmdx_at = 0, mmid_at = 0, mwmo_at = 0, mwid_at = 0;
      std::size_t mddf_at = 0, modf_at = 0, mh2o_at = 0, mfbo_at = 0, mtxf_at = 0;

      if constexpr (requires { this->textures; })
        mtex_at = emit("MTEX", [&] { (void)this->textures.write(out); });
      mmdx_at = emit("MMDX", [&] { (void)model_filenames.write(out); });
      mmid_at = emit("MMID", [&] { put(model_name_offsets.data(), model_name_offsets.size() * 4); });
      mwmo_at = emit("MWMO", [&] { (void)wmo_filenames.write(out); });
      mwid_at = emit("MWID", [&] { put(wmo_name_offsets.data(), wmo_name_offsets.size() * 4); });
      mddf_at = emit("MDDF", [&] {
        put(doodad_placements.data(), doodad_placements.size() * sizeof(common::SMDoodadDef));
      });
      modf_at = emit("MODF", [&] {
        put(wmo_placements.data(), wmo_placements.size() * sizeof(common::SMMapObjDef));
      });
      if constexpr (requires { this->water; })
        if (!this->water.empty())
          mh2o_at = emit("MH2O", [&] { (void)this->water.write(out); });

      std::array<std::pair<std::size_t, std::size_t>, 256> mcnk_loc{};  // (fourcc pos, payload size)
      for (std::size_t i = 0; i < cells.size() && i < 256; ++i)
      {
        std::optional<Error> err;
        const std::size_t at = emit("MCNK", [&] {
          const std::size_t before = out.size();
          if (auto r = cells[i].write_slice(out, FileKind::monolithic, alpha_format); !r)
            err = r.error();
          (void)before;
        });
        if (err)
          return std::unexpected{*err};
        mcnk_loc[i] = {at, out.size() - (at + 8)};
      }

      const bool has_mfbo =
        header.flags & static_cast<std::uint32_t>(MapHeaderFlags::has_mfbo);
      if constexpr (requires { this->flying_bounds; })
        if (has_mfbo)
          mfbo_at = emit("MFBO", [&] { put(&this->flying_bounds, sizeof(MFBOPlanes)); });
      if constexpr (requires { this->texture_flags; })
        if (!this->texture_flags.empty())
          mtxf_at = emit("MTXF", [&] {
            put(this->texture_flags.data(), this->texture_flags.size() * sizeof(SMTextureFlags));
          });

      // patch MCIN: absolute offset at the MCNK fourcc, size including the header
      for (std::size_t i = 0; i < 256; ++i)
      {
        std::uint32_t entry[4]{static_cast<std::uint32_t>(mcnk_loc[i].first),
                               static_cast<std::uint32_t>(mcnk_loc[i].second + 8), 0, 0};
        std::memcpy(out.data() + mcin_at + 8 + i * 16, entry, 16);
      }
      // patch MHDR offsets: relative to the MHDR data start, pointing at the
      // target chunk's fourcc.
      const std::size_t base = mhdr_at + 8;
      const auto rel = [&](std::size_t at) {
        return at == 0 ? 0u : static_cast<std::uint32_t>(at - base);
      };
      MHDRData h = header;
      h.ofs_mcin = rel(mcin_at);
      h.ofs_mtex = rel(mtex_at);
      h.ofs_mmdx = rel(mmdx_at);
      h.ofs_mmid = rel(mmid_at);
      h.ofs_mwmo = rel(mwmo_at);
      h.ofs_mwid = rel(mwid_at);
      h.ofs_mddf = rel(mddf_at);
      h.ofs_modf = rel(modf_at);
      h.ofs_mfbo = rel(mfbo_at);
      h.ofs_mh2o = rel(mh2o_at);
      h.ofs_mtxf = rel(mtxf_at);
      std::memcpy(out.data() + mhdr_at + 8, &h, sizeof(MHDRData));
      return out;
    }

    template <ClientVersion V>
    Result<FileBuffer> ADT<V>::write_split_file(FileKind kind) const
    {
      FileBuffer out;
      const auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
      };
      const auto emit = [&](const char (&cc)[5], auto&& body) -> std::size_t {
        const std::size_t at = out.size();
        std::uint32_t magic = four_cc(cc);
        put(&magic, 4);
        const std::size_t size_at = out.size();
        out.insert(out.end(), 4, std::byte{0});
        body();
        const auto size = static_cast<std::uint32_t>(out.size() - size_at - 4);
        std::memcpy(out.data() + size_at, &size, 4);
        return at;
      };

      emit("MVER", [&] { std::uint32_t v = adt_version_18; put(&v, 4); });

      std::size_t mh2o_at = 0, mfbo_at = 0, mhdr_at = 0;
      std::optional<Error> err;
      const auto emit_cells = [&] {
        for (std::size_t i = 0; i < cells.size() && i < 256; ++i)
          emit("MCNK", [&] {
            if (auto r = cells[i].write_slice(out, kind, alpha_format); !r)
              err = r.error();
          });
      };

      if (kind == FileKind::root)
      {
        mhdr_at = emit("MHDR", [&] { put(&header, sizeof(MHDRData)); });
        if constexpr (requires { this->water; })
          if (!this->water.empty())
            mh2o_at = emit("MH2O", [&] { (void)this->water.write(out); });
        emit_cells();
        const bool has_mfbo =
          header.flags & static_cast<std::uint32_t>(MapHeaderFlags::has_mfbo);
        if constexpr (requires { this->flying_bounds; })
          if (has_mfbo)
            mfbo_at = emit("MFBO", [&] { put(&this->flying_bounds, sizeof(MFBOPlanes)); });
        // stamp the two offsets a split root actually carries (rest stay 0; the
        // reader zeroes them all anyway).
        const std::size_t hbase = mhdr_at + 8;
        MHDRData h = header;
        h.ofs_mh2o = mh2o_at ? static_cast<std::uint32_t>(mh2o_at - hbase) : 0;
        h.ofs_mfbo = mfbo_at ? static_cast<std::uint32_t>(mfbo_at - hbase) : 0;
        std::memcpy(out.data() + mhdr_at + 8, &h, sizeof(MHDRData));
      }
      else if (kind == FileKind::tex0)
      {
        if constexpr (requires { this->mamp; })
          emit("MAMP", [&] { put(&this->mamp, 4); });
        bool fdids = false;
        if constexpr (requires { this->uses_texture_fdids; })
          fdids = this->uses_texture_fdids;
        if (fdids)
        {
          if constexpr (requires { this->diffuse_texture_ids; })
          {
            emit("MDID", [&] {
              put(this->diffuse_texture_ids.data(), this->diffuse_texture_ids.size() * 4);
            });
            emit("MHID", [&] {
              put(this->height_texture_ids.data(), this->height_texture_ids.size() * 4);
            });
          }
        }
        else
          emit("MTEX", [&] { (void)textures.write(out); });
        emit_cells();
        if constexpr (requires { this->texture_params; })
          if (!this->texture_params.empty())
            emit("MTXP", [&] {
              put(this->texture_params.data(),
                  this->texture_params.size() * sizeof(SMTextureParams));
            });
      }
      else if (kind == FileKind::obj0)
      {
        emit("MMDX", [&] { (void)model_filenames.write(out); });
        emit("MMID", [&] { put(model_name_offsets.data(), model_name_offsets.size() * 4); });
        emit("MWMO", [&] { (void)wmo_filenames.write(out); });
        emit("MWID", [&] { put(wmo_name_offsets.data(), wmo_name_offsets.size() * 4); });
        emit("MDDF", [&] {
          put(doodad_placements.data(), doodad_placements.size() * sizeof(common::SMDoodadDef));
        });
        emit("MODF", [&] {
          put(wmo_placements.data(), wmo_placements.size() * sizeof(common::SMMapObjDef));
        });
        emit_cells();
      }
      if (err)
        return std::unexpected{*err};
      return out;
    }

    template <ClientVersion V>
    Result<void> ADT<V>::read(fs::FileSystem& fs, const FileKey& key)
    {
      *this = ADT{};
      alpha_format = resolve_alpha_format(fs, key);

      if constexpr (V < builds::Cata)
      {
        const auto data = fs.read_file(key);
        if (!data)
          return std::unexpected{data.error()};
        cells.assign(256, adt::MapChunk<V>{});
        if (auto r = parse_file(*data, FileKind::monolithic); !r)
          return r;
        normalize_cells();
        return {};
      }
      else
      {
        // Cata+ split tile: the root .adt plus its _tex0/_obj0/_obj1/_lod
        // satellites, located by the "{stem}_<suffix>.adt" naming convention.
        // root/tex0/obj0 are parsed and MERGED into the one entity (their 256
        // MCNK streams accumulate per cell); _obj1/_lod are preserved verbatim
        // this stage (structured later).
        const FileKey resolved = fs.resolve(key);
        if (!resolved.path)
          return make_error(ErrorCode::PathNotResolvable,
                            "loading a split ADT needs the root file path");
        std::string_view stem = *resolved.path;
        if (stem.ends_with(".adt"))
          stem.remove_suffix(4);
        const auto sibling = [&](std::string_view suffix) {
          return std::format("{}{}.adt", stem, suffix);
        };

        const auto root_data = fs.read_file(key);
        if (!root_data)
          return std::unexpected{root_data.error()};
        cells.assign(256, adt::MapChunk<V>{});
        if (auto r = parse_file(*root_data, FileKind::root); !r)
          return r;

        const auto load = [&](std::string_view suffix, FileKind fk) -> Result<void> {
          const FileKey k{sibling(suffix)};
          if (!fs.exists(k))
            return {};
          const auto data = fs.read_file(k);
          if (!data)
            return make_error(data.error().code,
                              std::format("{} satellite: {}", suffix, data.error().message));
          return parse_file(*data, fk);
        };
        if (auto r = load("_tex0", FileKind::tex0); !r)
          return r;
        if (auto r = load("_obj0", FileKind::obj0); !r)
          return r;

        // preserve the unmodeled satellites verbatim
        if constexpr (requires { this->obj1_data; })
        {
          const auto keep = [&](std::string_view suffix, std::vector<std::byte>& into) {
            const FileKey k{sibling(suffix)};
            if (fs.exists(k))
              if (const auto data = fs.read_file(k))
                into = *data;
          };
          keep("_obj1", this->obj1_data);
          keep("_lod", this->lod_data);
        }

        normalize_cells();
        return {};
      }
    }

    template <ClientVersion V>
    Result<void> ADT<V>::write(fs::FileSystem& fs, const FileKey& key) const
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return make_error(ErrorCode::PathNotResolvable, "saving an ADT needs a path");

      // add_file returns Result<FileDataID>; a save only cares whether it failed.
      const auto add = [&](std::string_view path,
                           std::span<const std::byte> bytes) -> Result<void> {
        if (auto r = fs.add_file(path, bytes); !r)
          return std::unexpected{r.error()};
        return {};
      };

      if constexpr (V < builds::Cata)
      {
        const auto data = write_monolithic();
        if (!data)
          return std::unexpected{data.error()};
        return add(*resolved.path, *data);
      }
      else
      {
        std::string_view stem = *resolved.path;
        if (stem.ends_with(".adt"))
          stem.remove_suffix(4);
        const auto sibling = [&](std::string_view suffix) {
          return std::format("{}{}.adt", stem, suffix);
        };
        const auto store = [&](FileKind fk, std::string_view suffix) -> Result<void> {
          const auto data = write_split_file(fk);
          if (!data)
            return std::unexpected{data.error()};
          return add(sibling(suffix), *data);
        };
        // the root file keeps the bare "{stem}.adt" name
        {
          const auto data = write_split_file(FileKind::root);
          if (!data)
            return std::unexpected{data.error()};
          if (auto r = add(*resolved.path, *data); !r)
            return r;
        }
        if (auto r = store(FileKind::tex0, "_tex0"); !r)
          return r;
        if (auto r = store(FileKind::obj0, "_obj0"); !r)
          return r;
        if constexpr (requires { this->obj1_data; })
        {
          if (!this->obj1_data.empty())
            if (auto r = add(sibling("_obj1"), this->obj1_data); !r)
              return r;
          if (!this->lod_data.empty())
            if (auto r = add(sibling("_lod"), this->lod_data); !r)
              return r;
        }
        return {};
      }
    }
  }
}
