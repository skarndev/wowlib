#pragma once

/** @file
    The ADT terrain-tile entity (namespace wowlib::formats::adt): ADT<V>, one map
    tile, unified across the split ADT files it is stored in.

    A tile is 16x16 = 256 terrain chunks (MapChunk) plus tile-wide texture, model
    and placement tables. Pre-Cataclysm it is a single .adt; Cataclysm split it
    into a root .adt and _tex0/_obj0/_obj1(/_lod) split files, DISTRIBUTING the
    same chunks (and each terrain chunk's MCNK sub-chunks) across them. wowlib
    models ONE ADT<V> holding everything: the reader loads every file of the tile
    and merges them, the writer re-distributes on save — so user code adds a
    texture or a doodad without caring which file it lands in. Version-gated tile
    chunks live in conditionally-inherited trait bases (adt::detail).

    Even though ADT is NOT a ChunkedFile (the MCNK stream needs a multi-arg
    per-file read_from, merges across files, and routes chunks to different
    physical files on write — none of which the chunk engine expresses; see
    adt-architecture.md), the TILE-LEVEL chunks are uniform, so their members
    carry `chunk()` + `in_file()` annotations and parse_file/write_file are driven
    by reflection over them. MCNK and the derived MHDR/MCIN offset tables are the
    only hand-written cases.

    The on-disk MCAL alpha-map bit depth is a per-MAP property (the WDT MPHD
    big-alpha flags) that wowlib does NOT resolve for you — the caller passes the
    AlphaFormat to read() and write() explicitly. ADT does not guarantee a
    byte-identical round-trip (alpha maps are re-encoded, the MHDR/MCIN/MCNK offset
    tables are re-derived): the contract is a semantic round-trip,
    parse(write(x)) == parse(x). */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
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
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/map_placements.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/version_range.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::adt
{
  using namespace wowlib::formats::adt::chunks;

  // InFile / in_file() / routes_to() / FileKind are defined in map_chunk.hpp and
  // shared: the same physical-file routing drives both the tile-level chunks here
  // and the MCNK sub-chunks there.

  /** The version-agnostic base of every ADT<V> (welded as "ADT"): the language
      bindings attach for_version/read/write/convert here. No role in the C++ API,
      where you use the concrete ADT<V> directly. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("ADT"),
    =welder::doc(R"(
        A terrain map tile, abstract over the client version — the .adt file (and,
        since Cataclysm, its _tex0/_obj0/_obj1/_lod split files) as one entity.
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
      [[=chunk("MFBO"),
        =in_file(InFile::root),
        =welder::doc("The flying bounds (MFBO, BC+); engaged by the header has_mfbo flag.")]]
      MFBOPlanes flying_bounds{};
    };

    /** WotLK+ tile chunks: the water (MH2O) and the texture flags (MTXF). */
    struct ADTWotlk
    {
      [[=chunk("MH2O"),
        =in_file(InFile::root),
        =formats::optional,
        =welder::doc("The tile's water (MH2O, WotLK+): one liquid entry per chunk.")]]
      MH2OData water{};

      [[=chunk("MTXF"),
        =in_file(InFile::tex),
        =formats::optional,
        =welder::doc("Per-texture flags (MTXF, WotLK+): one entry per MTEX texture."),
        =welder::mark::no_reassign]]
      std::vector<SMTextureFlags> texture_flags;
    };

    /** Cataclysm+ split-file tile chunks (the tile now spans root/_tex0/_obj0/
        _obj1/_lod files). MTXP is strictly MoP+ but harmless empty on Cata; the
        _obj1 and _lod files are preserved verbatim this stage (structured in a
        later one). */
    struct ADTSplit
    {
      [[=chunk("MAMP"),
        =in_file(InFile::tex),
        =welder::doc("The MAMP alpha-map downscale value (Cata+): overrides the MHDR "
                     "inline value; alpha texture size is 64 / (2^value).")]]
      std::uint32_t mamp = 0;

      [[=welder::doc("Whether this tile stores its textures as MDID/MHID FileDataIDs "
                     "(8.1+ height-texturing maps) rather than MTEX names; set from the "
                     "chunk present on read and honored on write.")]]
      bool uses_texture_fdids = false;

      [[=chunk("MTXP"),
        =in_file(InFile::tex),
        =formats::optional,
        =welder::doc("Height-blend texture parameters (MTXP, MoP+): one per texture."),
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
      [[=chunk("MDID"),
        =in_file(InFile::tex),
        =formats::optional,
        =welder::doc("Diffuse-texture FileDataIDs (MDID, 8.1+): the _s.blp tileset "
                     "textures MapChunk layers index, in place of MTEX names."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> diffuse_texture_ids;

      [[=chunk("MHID"),
        =in_file(InFile::tex),
        =formats::optional,
        =welder::doc("Height-texture FileDataIDs (MHID, 8.1+): the _h.blp map paired with "
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
          A terrain map tile for one client version: the 256 terrain chunks plus the
          tile-wide texture, model and placement tables, unified across the split ADT
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

      /** The canonical top-level chunk order (a superset; each physical file emits
          the subset routed to it, in this order — see write_file). MCIN and MCNK
          are positional placeholders the writer fills specially. */
      static constexpr std::array chunk_order{
        four_cc("MVER"), four_cc("MHDR"), four_cc("MCIN"), four_cc("MAMP"),
        four_cc("MTEX"), four_cc("MDID"), four_cc("MHID"), four_cc("MMDX"),
        four_cc("MMID"), four_cc("MWMO"), four_cc("MWID"), four_cc("MDDF"),
        four_cc("MODF"), four_cc("MH2O"), four_cc("MCNK"), four_cc("MFBO"),
        four_cc("MTXF"), four_cc("MTXP")};

      [[=chunk("MVER"),
        =welder::doc("The ADT format version (MVER); 18 for every supported client.")]]
      std::uint32_t mver = adt_version_18;

      [[=chunk("MHDR"),
        =in_file(InFile::root),
        =welder::doc("The tile header (MHDR): flags; the chunk offsets are derived.")]]
      MHDRData header{};

      [[=chunk("MTEX"),
        =in_file(InFile::tex),
        =welder::doc("The tileset texture filenames (MTEX): the paths MapChunk layers "
                     "index. Present unless the tile uses MDID/MHID FileDataIDs (8.1+ "
                     "height-texturing maps)."),
        =welder::mark::no_reassign]]
      StringBlock textures;

      [[=chunk("MMDX"),
        =in_file(InFile::obj),
        =welder::doc("The M2 model filenames (MMDX) placements reference by MMID index."),
        =welder::mark::no_reassign]]
      StringBlock model_filenames;

      [[=chunk("MMID"),
        =in_file(InFile::obj),
        =welder::doc("Byte offsets into model_filenames (MMID): a doodad placement's "
                     "name_id indexes this list."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> model_name_offsets;

      [[=chunk("MWMO"),
        =in_file(InFile::obj),
        =welder::doc("The WMO filenames (MWMO) placements reference by MWID index."),
        =welder::mark::no_reassign]]
      StringBlock wmo_filenames;

      [[=chunk("MWID"),
        =in_file(InFile::obj),
        =welder::doc("Byte offsets into wmo_filenames (MWID): a WMO placement's name_id "
                     "indexes this list."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> wmo_name_offsets;

      [[=chunk("MDDF"),
        =in_file(InFile::obj),
        =welder::doc("Doodad (M2) placements on this tile (MDDF)."),
        =welder::mark::no_reassign]]
      std::vector<common::SMDoodadDef> doodad_placements;

      [[=chunk("MODF"),
        =in_file(InFile::obj),
        =welder::doc("WMO placements on this tile (MODF)."),
        =welder::mark::no_reassign]]
      std::vector<common::SMMapObjDef> wmo_placements;

      [[=welder::doc("The 256 terrain chunks (MCNK), row-major (index = y * 16 + x)."),
        =welder::mark::no_reassign]]
      std::vector<adt::MapChunk<V>> chunks;

      [[=welder::doc("How this tile's alpha maps were laid out on disk, recorded from the "
                     "AlphaFormat passed to read(); write() takes its own explicit "
                     "argument. wowlib always presents decoded 64x64 maps.")]]
      AlphaFormat alpha_format = AlphaFormat::lowres_4bit;

      // --- fs I/O (definitions at the bottom of this header) ------------------

      [[=welder::mark::only(welder::lang::lua),
        =welder::doc("Load the tile — every split file present — from a client "
                     "filesystem, replacing this entity's contents. The alpha-map bit "
                     "depth (from the map's WDT) is supplied by the caller.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key
                        [[=welder::doc("the tile identity (root .adt path and/or "
                                       "FileDataID)")]],
                        AlphaFormat alpha
                        [[=welder::doc("the on-disk alpha-map bit depth for this tile's "
                                       "map (from its WDT MPHD flags)")]]);

      [[=welder::mark::only(welder::lang::lua),
        =welder::doc("Serialize the tile (and, Cata+, every split file) through the "
                     "filesystem's project overlay; the file names derive from the key, "
                     "which must resolve to a path. The alpha-map bit depth to encode is "
                     "supplied by the caller.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key
                         [[=welder::doc("the tile identity; must resolve to a path")]],
                         AlphaFormat alpha
                         [[=welder::doc("the on-disk alpha-map bit depth to encode")]]) const;

      /** Parse one split file's chunk stream into this entity (merging), by
          reflecting over the `chunk()`-annotated members.
          @param data the file bytes.
          @param kind which split file it is.
          @return a structural error or success. */
      [[=welder::mark::exclude]]
      Result<void> parse_file(std::span<const std::byte> data, FileKind kind);

      /** Serialize one physical file of the tile (monolithic pre-Cata, or one of
          root/_tex0/_obj0), routing each `chunk()`-annotated member by its
          `in_file` group and stamping the derived MHDR/MCIN tables.
          @param kind  which physical file to write.
          @param alpha the alpha-map bit depth to encode.
          @return the file bytes or a structural error. */
      [[=welder::mark::exclude]]
      Result<FileBuffer> write_file(FileKind kind, AlphaFormat alpha) const;

    private:
      /** Normalize every chunk's do_not_fix_alpha flag to set (we hold full 64x64
          maps): called once after all of a tile's files are read. */
      void normalize_chunks();

      /** Zero the MHDR chunk offsets after reading it: they describe an on-disk
          layout wowlib re-derives on write, so keeping them would make the
          semantic round-trip compare layout artifacts. Flags and MAMP stay. */
      void _normalize_mhdr()
      {
        header.ofs_mcin = header.ofs_mtex = header.ofs_mmdx = header.ofs_mmid = 0;
        header.ofs_mwmo = header.ofs_mwid = header.ofs_mddf = header.ofs_modf = 0;
        header.ofs_mfbo = header.ofs_mh2o = header.ofs_mtxf = 0;
      }

      /** Whether a `chunk()`-annotated member is emitted for the current write.
          Required members (no `optional`) always write (clients ship size-0
          required chunks); `optional` ones only when non-empty. Two members carry
          bespoke rules: MFBO tracks the header flag, and MTEX/MDID/MHID encode the
          name-vs-FileDataID texture scheme (uses_texture_fdids).
          @tparam m the reflected member.
          @return whether to emit the member's chunk. */
      template <std::meta::info m>
      bool _write_engaged() const
      {
        constexpr std::string_view id = std::meta::identifier_of(m);
        if constexpr (id == "flying_bounds")
          return has_flag(header.flags, MapHeaderFlags::has_mfbo);
        else if constexpr (id == "textures")
        {
          if constexpr (requires { this->uses_texture_fdids; })
            return !this->uses_texture_fdids;  // MTEX unless the tile uses FileDataIDs
          else
            return true;  // pre-8.1: always MTEX
        }
        else if constexpr (id == "diffuse_texture_ids" || id == "height_texture_ids")
        {
          if constexpr (requires { this->uses_texture_fdids; })
            return this->uses_texture_fdids;
          else
            return false;
        }
        else if constexpr (formats::detail::annotation<formats::detail::optional_spec, m>()
                             .has_value())
          return !this->[:m:].empty();
        else
          return true;
      }

      /** Append @a n raw bytes at @a p to @a out.
          @param out the destination buffer.
          @param p   the source bytes.
          @param n   the byte count. */
      static void _put(FileBuffer& out, const void* p, std::size_t n)
      {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
      }

      /** Emit one top-level chunk (fourcc + size + body) into @a out and return
          the fourcc position (for MHDR/MCIN offset stamping).
          @tparam Body the payload-writing callable type.
          @param out   the destination buffer.
          @param magic the chunk fourcc.
          @param body  writes the chunk payload into @a out.
          @return the chunk's fourcc position in @a out. */
      template <typename Body>
      static std::size_t _emit_chunk(FileBuffer& out, std::uint32_t magic, Body&& body)
      {
        const std::size_t at = out.size();
        _put(out, &magic, 4);
        const std::size_t size_at = out.size();
        out.insert(out.end(), 4, std::byte{0});
        body();
        const auto size = static_cast<std::uint32_t>(out.size() - size_at - 4);
        std::memcpy(out.data() + size_at, &size, 4);
        return at;
      }
    };
  }

  /** A terrain tile — the canonicalizing face of detail::ADT. */
  template <ClientVersion V>
  using ADT = detail::ADT<canonical_version(V, adt_pivots, adt_versions)>;

  namespace detail
  {
    template <ClientVersion V>
    void ADT<V>::normalize_chunks()
    {
      for (auto& chunk : chunks)
        chunk.header.flags |= std::to_underlying(MapChunkFlags::do_not_fix_alpha_map);
    }

    template <ClientVersion V>
    Result<void> ADT<V>::parse_file(std::span<const std::byte> data, FileKind kind)
    {
      using Self = ADT<V>;
      static constexpr auto members = formats::detail::members_of<Self>();

      std::size_t pos = 0;
      std::size_t chunk_index = 0;
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

        // MCNK is the one non-reflective chunk: 256 repeats, each a per-file
        // portion merged into a MapChunk via read_from.
        if (magic == four_cc("MCNK"))
        {
          if (chunks.size() < chunk_index + 1)
            chunks.resize(chunk_index + 1);
          if (auto r = chunks[chunk_index].read_from(payload, kind, alpha_format); !r)
            return r;
          ++chunk_index;
          continue;
        }

        // Every other chunk routes to its `chunk()`-annotated member by fourcc.
        // Version-gated members live in trait bases: for a version where a trait
        // is inactive the member is absent from `members`, so an unmodeled chunk
        // (e.g. MCIN, whose fields are derived and needs no member) simply does
        // not match and is skipped.
        Result<void> outcome{};
        bool matched = false;
        template for (constexpr auto m : members)
        {
          if constexpr (constexpr auto spec =
                          formats::detail::annotation<formats::detail::chunk_spec, m>();
                        spec.has_value())
          {
            if (!matched && magic == spec->magic)
            {
              matched = true;
              outcome =
                formats::detail::read_value(this->[:m:], payload, magic, pos, spec->endian);
              // the two members that need a post-read fix-up: MHDR's derived
              // offsets are zeroed, and MDID flips the texture-scheme flag.
              if constexpr (std::meta::identifier_of(m) == "header")
                _normalize_mhdr();
              else if constexpr (std::meta::identifier_of(m) == "diffuse_texture_ids")
                if constexpr (requires { this->uses_texture_fdids; })
                  this->uses_texture_fdids = true;
            }
          }
        }
        if (!outcome)
          return outcome;
      }
      return {};
    }

    template <ClientVersion V>
    Result<FileBuffer> ADT<V>::write_file(FileKind kind, AlphaFormat alpha) const
    {
      using Self = ADT<V>;
      static constexpr auto members = formats::detail::members_of<Self>();
      const bool mono = kind == FileKind::monolithic;

      FileBuffer out;
      std::size_t mhdr_at = 0, mcin_at = 0;
      std::array<std::pair<std::size_t, std::size_t>, 256> mcnk_loc{};  // (fourcc pos, payload size)
      // Emitted chunk fourcc positions, for the MHDR offset table.
      std::array<std::pair<std::uint32_t, std::size_t>, chunk_order.size()> emitted{};
      std::size_t n_emitted = 0;
      std::optional<Error> err;

      const auto record = [&](std::uint32_t magic, std::size_t at) {
        emitted[n_emitted++] = {magic, at};
      };
      const auto position_of = [&](std::uint32_t magic) -> std::size_t {
        for (std::size_t i = 0; i < n_emitted; ++i)
          if (emitted[i].first == magic)
            return emitted[i].second;
        return 0;
      };

      for (const std::uint32_t want : chunk_order)
      {
        if (want == four_cc("MVER"))
        {
          _emit_chunk(out, want, [&] { const std::uint32_t v = adt_version_18; _put(out, &v, 4); });
          continue;
        }
        if (want == four_cc("MCIN"))
        {
          if (mono)
            mcin_at = _emit_chunk(out, want, [&] { out.insert(out.end(), 256 * 16, std::byte{0}); });
          continue;
        }
        if (want == four_cc("MCNK"))
        {
          for (std::size_t i = 0; i < chunks.size() && i < 256; ++i)
          {
            const std::size_t at = _emit_chunk(out, want, [&] {
              if (auto r = chunks[i].write_to(out, kind, alpha); !r)
                err = r.error();
            });
            mcnk_loc[i] = {at, out.size() - (at + 8)};
          }
          continue;
        }

        // Reflective emit: the member whose chunk() magic == want, if it is routed
        // to this file and engaged.
        template for (constexpr auto m : members)
        {
          if constexpr (constexpr auto spec =
                          formats::detail::annotation<formats::detail::chunk_spec, m>();
                        spec.has_value())
          {
            if constexpr (constexpr auto route =
                            formats::detail::annotation<in_file_spec, m>();
                          route.has_value())
            {
              if (spec->magic == want && routes_to(route->file, kind) && _write_engaged<m>())
              {
                // Splice the member reference HERE (m is a constant expression);
                // inside the lambda m is captured by reference and would not be.
                const auto& member = this->[:m:];
                const std::size_t at = _emit_chunk(out, want, [&] {
                  if (auto r = formats::detail::write_value(member, out); !r)
                    err = r.error();
                });
                record(want, at);
                if (want == four_cc("MHDR"))
                  mhdr_at = at;
              }
            }
          }
        }
      }
      if (err)
        return std::unexpected{*err};

      // Stamp the derived tables. MCIN: absolute offset at each MCNK fourcc, size
      // including the 8-byte header (monolithic only). MHDR: offsets relative to
      // the MHDR data start, pointing at each target chunk's fourcc.
      if (mono)
        for (std::size_t i = 0; i < 256; ++i)
        {
          const std::uint32_t entry[4]{static_cast<std::uint32_t>(mcnk_loc[i].first),
                                       static_cast<std::uint32_t>(mcnk_loc[i].second + 8), 0, 0};
          std::memcpy(out.data() + mcin_at + 8 + i * 16, entry, 16);
        }
      if (file_has_header(kind))
      {
        const std::size_t base = mhdr_at + 8;
        const auto rel = [&](std::uint32_t magic) {
          const std::size_t at = position_of(magic);
          return at == 0 ? 0u : static_cast<std::uint32_t>(at - base);
        };
        MHDRData h = header;
        h.ofs_mcin = mcin_at == 0 ? 0u : static_cast<std::uint32_t>(mcin_at - base);
        h.ofs_mtex = rel(four_cc("MTEX"));
        h.ofs_mmdx = rel(four_cc("MMDX"));
        h.ofs_mmid = rel(four_cc("MMID"));
        h.ofs_mwmo = rel(four_cc("MWMO"));
        h.ofs_mwid = rel(four_cc("MWID"));
        h.ofs_mddf = rel(four_cc("MDDF"));
        h.ofs_modf = rel(four_cc("MODF"));
        h.ofs_mfbo = rel(four_cc("MFBO"));
        h.ofs_mh2o = rel(four_cc("MH2O"));
        h.ofs_mtxf = rel(four_cc("MTXF"));
        std::memcpy(out.data() + mhdr_at + 8, &h, sizeof(MHDRData));
      }
      return out;
    }

    template <ClientVersion V>
    Result<void> ADT<V>::read(fs::FileSystem& fs, const FileKey& key, AlphaFormat alpha)
    {
      *this = ADT{};
      alpha_format = alpha;

      if constexpr (V < builds::Cata)
      {
        // The pre-Cata tile is a single monolithic .adt carrying every chunk.
        return fs.read_file(key).and_then([&](FileBuffer data) -> Result<void> {
          chunks.assign(256, adt::MapChunk<V>{});
          if (auto r = parse_file(data, FileKind::monolithic); !r)
            return r;
          normalize_chunks();
          return {};
        });
      }
      else
      {
        // Cata+ split tile: the root .adt plus its _tex0/_obj0/_obj1/_lod split
        // files, located by the "{stem}_<suffix>.adt" naming convention.
        // root/tex0/obj0 are parsed and MERGED into the one entity (their 256
        // MCNK streams accumulate per chunk); _obj1/_lod are preserved verbatim
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
        chunks.assign(256, adt::MapChunk<V>{});
        if (auto r = parse_file(*root_data, FileKind::root); !r)
          return r;

        const auto load = [&](std::string_view suffix, FileKind fk) -> Result<void> {
          const FileKey k{sibling(suffix)};
          if (!fs.exists(k))
            return {};
          const auto data = fs.read_file(k);
          if (!data)
            return make_error(data.error().code,
                              std::format("{} split file: {}", suffix, data.error().message));
          return parse_file(*data, fk);
        };
        if (auto r = load("_tex0", FileKind::tex0); !r)
          return r;
        if (auto r = load("_obj0", FileKind::obj0); !r)
          return r;

        // preserve the unmodeled split files verbatim
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

        normalize_chunks();
        return {};
      }
    }

    template <ClientVersion V>
    Result<void> ADT<V>::write(fs::FileSystem& fs, const FileKey& key, AlphaFormat alpha) const
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return make_error(ErrorCode::PathNotResolvable, "saving an ADT needs a path");

      // add_file returns Result<FileDataID>; a save only cares whether it failed.
      const auto add = [&](std::string_view path,
                           std::span<const std::byte> bytes) -> Result<void> {
        return fs.add_file(path, bytes).transform([](auto&&) {});
      };

      if constexpr (V < builds::Cata)
      {
        return write_file(FileKind::monolithic, alpha)
          .and_then([&](FileBuffer data) { return add(*resolved.path, data); });
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
          return write_file(fk, alpha).and_then(
            [&](FileBuffer data) { return add(sibling(suffix), data); });
        };
        // the root file keeps the bare "{stem}.adt" name
        if (auto r = write_file(FileKind::root, alpha)
                       .and_then([&](FileBuffer data) { return add(*resolved.path, data); });
            !r)
          return r;
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
