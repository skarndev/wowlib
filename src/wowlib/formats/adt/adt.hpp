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
    per-file readFrom, merges across files, and routes chunks to different
    physical files on write — none of which the chunk engine expresses; see
    adt-architecture.md), the TILE-LEVEL chunks are uniform, so their members
    carry `chunk()` + `inFile()` annotations and parseFile/writeFile are driven
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
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/adt/boundaries.hpp>
#include <wowlib/formats/adt/chunks/header.hpp>
#include <wowlib/formats/adt/chunks/texture.hpp>
#include <wowlib/formats/adt/liquid.hpp>
#include <wowlib/formats/adt/map_chunk.hpp>
#include <wowlib/formats/common/file_entity.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/map_placements.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/version_range.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::adt {
  using namespace wowlib::formats::adt::chunks;

  // InFile / inFile() / routesTo() / FileKind are defined in map_chunk.hpp and
  // shared: the same physical-file routing drives both the tile-level chunks here
  // and the MCNK sub-chunks there.

  /** The version-agnostic base of every ADT<V> (welded as "ADT"): the language
      bindings attach for_version/read/write/convert here. No role in the C++ API,
      where you use the concrete ADT<V> directly. */
  struct [[
      =welder::weld,
      =welder::weld_as("ADT"),
      WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A terrain map tile, abstract over the client version — the .adt file (and,
        since Cataclysm, its _tex0/_obj0/_obj1/_lod split files) as one entity.
        Construct the concrete version with ADT.for_version(expansion), then
        read()/write(); the per-version ADT* classes are subclasses. See
        https://wowdev.wiki/ADT/v18.)")
    ]] ADTBase : FileEntityBase {};

  namespace detail {
    /** The flying bounds (MFBO), BC+. Presence follows the MHDR HasMfbo flag. */
    struct ADTFlying {
      [[=chunk("MFBO"),
        =inFile(InFile::Root),
        =welder::doc(
          "The flying bounds (MFBO, BC+); engaged by the header has_mfbo flag.")
      ]]
      MFBOPlanes flyingBounds{};
    };

    /** WotLK+ tile chunks: the water (MH2O) and the texture flags (MTXF). */
    struct ADTWotlk {
      [[=chunk("MH2O"),
        =inFile(InFile::Root),
        =formats::Optional,
        =welder::doc(
          "The tile's water (MH2O, WotLK+): one liquid entry per chunk.")]]
      MH2OData water{};

      [[=chunk("MTXF"),
        =inFile(InFile::Tex),
        =formats::Optional,
        =welder::doc(
          "Per-texture flags (MTXF, WotLK+): one entry per MTEX texture."),
        =welder::mark::no_reassign]]
      std::vector<SMTextureFlags> textureFlags;
    };

    /** Cataclysm+ split-file tile chunks (the tile now spans root/_tex0/_obj0/
        _obj1/_lod files). MTXP is strictly MoP+ but harmless empty on Cata; the
        _obj1 and _lod files are preserved verbatim this stage (structured in a
        later one). */
    struct ADTSplit {
      [[=chunk("MAMP"),
        =inFile(InFile::Tex),
        =welder::doc(
          "The MAMP alpha-map downscale value (Cata+): overrides the MHDR "
          "inline value; alpha texture size is 64 / (2^value).")]]
      std::uint32_t mamp = 0;

      [[=welder::doc(
        "Whether this tile stores its textures as MDID/MHID FileDataIDs "
        "(8.1+ height-texturing maps) rather than MTEX names; set from the "
        "chunk present on read and honored on write.")]]
      bool usesTextureFdids = false;

      [[=chunk("MTXP"),
        =inFile(InFile::Tex),
        =formats::Optional,
        =welder::doc(
          "Height-blend texture parameters (MTXP, MoP+): one per texture."),
        =welder::mark::no_reassign]]
      std::vector<SMTextureParams> textureParams;

      [[=welder::mark::exclude]] std::vector<std::byte> obj1Data;
      // raw _obj1.adt
      [[=welder::mark::exclude]] std::vector<std::byte> lodData;
      // raw _lod.adt

      [[=welder::mark::exclude]]

      bool operator==(const ADTSplit&) const = default;
    };

    /** The 8.1+ FileDataID texture tables (_tex0), which replace MTEX names on
        height-texturing maps. */
    struct ADTTexFdids {
      [[=chunk("MDID"),
        =inFile(InFile::Tex),
        =formats::Optional,
        =welder::doc(
          "Diffuse-texture FileDataIDs (MDID, 8.1+): the _s.blp tileset "
          "textures MapChunk layers index, in place of MTEX names."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> diffuseTextureIds;

      [[=chunk("MHID"),
        =inFile(InFile::Tex),
        =formats::Optional,
        =welder::doc(
          "Height-texture FileDataIDs (MHID, 8.1+): the _h.blp map paired with "
          "each diffuse texture (0 for none)."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> heightTextureIds;

      [[=welder::mark::exclude]]

      bool operator==(const ADTTexFdids&) const = default;
    };
  }

  namespace detail {
    /** A terrain tile for one client version. Instantiate through the
        canonicalizing adt::ADT alias, never directly. */
    template <ClientVersion V>
    struct [[
        =welder::weld,
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
        Slot<V, builds::TBC, ADTFlying>,
        Slot<V, builds::WotLK, ADTWotlk>,
        Slot<V, builds::Cata, ADTSplit>,
        Slot<V, builds::BfA_TidesOfVengeance, ADTTexFdids> {
      static constexpr ClientVersion Version = V;

      /** The canonical top-level chunk order (a superset; each physical file emits
          the subset routed to it, in this order — see writeFile). MCIN and MCNK
          are positional placeholders the writer fills specially. */
      static constexpr std::array ChunkOrder{
        fourCc("MVER"),
        fourCc("MHDR"),
        fourCc("MCIN"),
        fourCc("MAMP"),
        fourCc("MTEX"),
        fourCc("MDID"),
        fourCc("MHID"),
        fourCc("MMDX"),
        fourCc("MMID"),
        fourCc("MWMO"),
        fourCc("MWID"),
        fourCc("MDDF"),
        fourCc("MODF"),
        fourCc("MH2O"),
        fourCc("MCNK"),
        fourCc("MFBO"),
        fourCc("MTXF"),
        fourCc("MTXP")
      };

      [[=chunk("MVER"),
        =welder::doc(
          "The ADT format version (MVER); 18 for every supported client.")]]
      std::uint32_t mver = AdtVersion18;

      [[=chunk("MHDR"),
        =inFile(InFile::Root),
        =welder::doc(
          "The tile header (MHDR): flags; the chunk offsets are derived.")]]
      MHDRData header{};

      [[=chunk("MTEX"),
        =inFile(InFile::Tex),
        =welder::doc(
          "The tileset texture filenames (MTEX): the paths MapChunk layers "
          "index. Present unless the tile uses MDID/MHID FileDataIDs (8.1+ "
          "height-texturing maps)."),
        =welder::mark::no_reassign]]
      StringBlock textures;

      [[=chunk("MMDX"),
        =inFile(InFile::Obj),
        =welder::doc(
          "The M2 model filenames (MMDX) placements reference by MMID index."),
        =welder::mark::no_reassign]]
      StringBlock modelFilenames;

      [[=chunk("MMID"),
        =inFile(InFile::Obj),
        =welder::doc(
          "Byte offsets into model_filenames (MMID): a doodad placement's "
          "name_id indexes this list."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> modelNameOffsets;

      [[=chunk("MWMO"),
        =inFile(InFile::Obj),
        =welder::doc(
          "The WMO filenames (MWMO) placements reference by MWID index."),
        =welder::mark::no_reassign]]
      StringBlock wmoFilenames;

      [[=chunk("MWID"),
        =inFile(InFile::Obj),
        =welder::doc(
          "Byte offsets into wmo_filenames (MWID): a WMO placement's name_id "
          "indexes this list."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> wmoNameOffsets;

      [[=chunk("MDDF"),
        =inFile(InFile::Obj),
        =welder::doc("Doodad (M2) placements on this tile (MDDF)."),
        =welder::mark::no_reassign]]
      std::vector<common::SMDoodadDef> doodadPlacements;

      [[=chunk("MODF"),
        =inFile(InFile::Obj),
        =welder::doc("WMO placements on this tile (MODF)."),
        =welder::mark::no_reassign]]
      std::vector<common::SMMapObjDef> wmoPlacements;

      [[=welder::doc(
          "The 256 terrain chunks (MCNK), row-major (index = y * 16 + x)."),
        =welder::mark::no_reassign]]
      std::vector<adt::MapChunk<V>> chunks;

      [[=welder::doc(
        "How this tile's alpha maps were laid out on disk, recorded from the "
        "AlphaFormat passed to read(); write() takes its own explicit "
        "argument. wowlib always presents decoded 64x64 maps.")]]
      AlphaFormat alphaFormat = AlphaFormat::Lowres4Bit;

      // --- fs I/O (definitions at the bottom of this header) ------------------

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
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

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc(
          "Serialize the tile (and, Cata+, every split file) through the "
          "filesystem's project overlay; the file names derive from the key, "
          "which must resolve to a path. The alpha-map bit depth to encode is "
          "supplied by the caller.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key [[=welder::doc("the tile identity; must resolve to a path")]],
                         AlphaFormat alpha [[=welder::doc("the on-disk alpha-map bit depth to encode")]]) const;

      // Beyond each terrain chunk's own contracts, this sees the tile-wide
      // ones: layers resolving in the tile's texture table (MTEX names or MDID
      // FileDataIDs, whichever the tile uses), chunk references landing in
      // MDDF/MODF, the placements' own name references, and the 256-chunk grid.
      [[nodiscard]]
      [[=welder::doc(R"(
          Check the logical integrity contracts this tile must satisfy to LOAD
          in the client — across every terrain chunk AND the tile-wide tables —
          which write() deliberately never enforces. Call it before writing when
          you want to know the tile will load. A tile read from a client and
          left unmodified reports no errors; warnings mark states real client
          files ship.)"),
        =welder::returns(R"(every violated contract, each with its member path
                            ("chunks[i]..."))")]]
      ValidationReport validate() const;

      [[nodiscard]]
      [[=welder::doc("Validate and raise on the first error instead of "
          "returning a report — the assert-style face of "
          "validate()."),
        =welder::returns("nothing; raises when validate() finds any error")]]
      Result<void> ensureValid() const;

      /** Parse one split file's chunk stream into this entity (merging), by
          reflecting over the `chunk()`-annotated members.
          @param data the file bytes.
          @param kind which split file it is.
          @return a structural error or success. */
      [[=welder::mark::exclude]]
      Result<void> parseFile(std::span<const std::byte> data, FileKind kind);

      /** Serialize one physical file of the tile (monolithic pre-Cata, or one of
          root/_tex0/_obj0), routing each `chunk()`-annotated member by its
          `inFile` group and stamping the derived MHDR/MCIN tables.
          @param kind  which physical file to write.
          @param alpha the alpha-map bit depth to encode.
          @return the file bytes or a structural error. */
      [[=welder::mark::exclude]]
      Result<FileBuffer> writeFile(FileKind kind, AlphaFormat alpha) const;

    private:
      /** Normalize every chunk's DoNotFixAlphaMap flag to set (we hold full 64x64
          maps): called once after all of a tile's files are read. */
      void normalizeChunks();

      /** Zero the MHDR chunk offsets after reading it: they describe an on-disk
          layout wowlib re-derives on write, so keeping them would make the
          semantic round-trip compare layout artifacts. Flags and MAMP stay. */
      void _normalizeMhdr() {
        header.ofsMcin = header.ofsMtex = header.ofsMmdx = header.ofsMmid = 0;
        header.ofsMwmo = header.ofsMwid = header.ofsMddf = header.ofsModf = 0;
        header.ofsMfbo = header.ofsMh2O = header.ofsMtxf = 0;
      }

      /** Whether a `chunk()`-annotated member is emitted for the current write.
          Required members (no `optional`) always write (clients ship size-0
          required chunks); `optional` ones only when non-empty. Two members carry
          bespoke rules: MFBO tracks the header flag, and MTEX/MDID/MHID encode the
          name-vs-FileDataID texture scheme (usesTextureFdids).
          @tparam M the reflected member.
          @return whether to emit the member's chunk. */
      template <std::meta::info M>
      bool _writeEngaged() const {
        constexpr std::string_view id = std::meta::identifier_of(M);
        if constexpr (id == "flyingBounds") return hasFlag(header.flags, MapHeaderFlags::HasMfbo);
        else if constexpr (id == "textures") {
          if constexpr (requires { this->usesTextureFdids; }) return !this->usesTextureFdids;
            // MTEX unless the tile uses FileDataIDs
          else return true; // pre-8.1: always MTEX
        }
        else if constexpr (id == "diffuseTextureIds" || id == "heightTextureIds") {
          if constexpr (requires { this->usesTextureFdids; }) return this->usesTextureFdids;
          else return false;
        }
        else if constexpr (formats::detail::annotation<formats::detail::OptionalSpec, M>().has_value()) return !this->
          [:M:].empty();
        else return true;
      }

      /** Append @a n raw bytes at @a p to @a out.
          @param out the destination buffer.
          @param p   the source bytes.
          @param n   the byte count. */
      static void _put(FileBuffer& out, const void* p, std::size_t n) {
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
      static std::size_t _emitChunk(FileBuffer& out, std::uint32_t magic, Body&& body) {
        const std::size_t at = out.size();
        _put(out, &magic, 4);
        const std::size_t sizeAt = out.size();
        out.insert(out.end(), 4, std::byte{0});
        body();
        const auto size = static_cast<std::uint32_t>(out.size() - sizeAt - 4);
        std::memcpy(out.data() + sizeAt, &size, 4);
        return at;
      }
    };
  }

  /** A terrain tile — the canonicalizing face of detail::ADT. */
  template <ClientVersion V>
  using ADT = detail::ADT<canonicalVersion(V, AdtPivots, AdtVersions)>;

  namespace detail {
    template <ClientVersion V>
    void ADT<V>::normalizeChunks() {
      for (auto& chunk : chunks)
        chunk.header.flags |= std::to_underlying(MapChunkFlags::DoNotFixAlphaMap);
    }

    template <ClientVersion V>
    ValidationReport ADT<V>::validate() const {
      ValidationReport report;

      // a tile is a full 16x16 grid; the client indexes chunks positionally
      if (!chunks.empty() && chunks.size() != ChunksPerTile)
        report.addError("chunks", std::format("count {} != the {} chunks of a tile", chunks.size(), ChunksPerTile));

      // the texture table a layer's textureId addresses: MTEX names, or the
      // MDID FileDataIDs once the tile uses them (a per-MAP choice, not a
      // version one - see usesTextureFdids)
      const std::size_t textureCount = [&] {
        if constexpr (requires { this->diffuseTextureIds; })
          if (this->usesTextureFdids) return this->diffuseTextureIds.size();
        return textures.entries().size();
      }();

      for (std::size_t i = 0; i < chunks.size() && !report.full(); ++i) {
        const std::size_t mark = report.size();
        const auto& chunk = chunks[i];
        formats::detail::validateEntity(chunk, report);

        for (std::size_t j = 0; j < chunk.layers.size(); ++j)
          if (chunk.layers[j].textureId >= textureCount)
            report.addError(std::format("layers[{}]", j),
                             std::format("texture_id {} out of range: {} textures", chunk.layers[j].textureId,
                                         textureCount));

        // the chunk's placement references index the TILE's tables
        formats::detail::validateIndexElements(chunk.doodadRefs, doodadPlacements.size(), "doodadRefs",
                                                 "doodadPlacements", report);
        formats::detail::validateIndexElements(chunk.objectRefs, wmoPlacements.size(), "objectRefs",
                                                 "wmoPlacements", report);
        report.prefixFrom(mark, std::format("chunks[{}]", i));
      }

      // A placement's nameId indexes the tile's name-offset table, EXCEPT
      // when its EntryIsFdid flag makes it a FileDataID the client loads
      // directly (Legion+) — then there is nothing local to resolve against.
      const auto checkPlacements = [&](const auto& placements,
                                        auto fdidFlag,
                                        const auto& offsets,
                                        std::string_view what,
                                        std::string_view table) {
        for (std::size_t i = 0; i < placements.size() && !report.full(); ++i) {
          if (hasFlag(placements[i].flags, fdidFlag)) continue;
          if (placements[i].nameId >= offsets.size())
            report.addError(std::format("{}[{}]", what, i),
                             std::format("name_id {} out of range: {} holds {} entries", placements[i].nameId, table,
                                         offsets.size()));
        }
      };
      checkPlacements(doodadPlacements, common::DoodadDefFlags::EntryIsFdid, modelNameOffsets,
                       "doodadPlacements", "modelNameOffsets");
      checkPlacements(wmoPlacements, common::MapObjDefFlags::EntryIsFdid, wmoNameOffsets, "wmoPlacements",
                       "wmoNameOffsets");

      return report;
    }

    template <ClientVersion V>
    Result<void> ADT<V>::ensureValid() const {
      return validate().toResult();
    }

    template <ClientVersion V>
    Result<void> ADT<V>::parseFile(std::span<const std::byte> data, FileKind kind) {
      using Self = ADT<V>;
      static constexpr auto Members = formats::detail::membersOf<Self>();

      std::size_t pos = 0;
      std::size_t chunkIndex = 0;
      while (pos + 8 <= data.size()) {
        std::uint32_t magic = 0, size = 0;
        std::memcpy(&magic, data.data() + pos, 4);
        std::memcpy(&size, data.data() + pos + 4, 4);
        if (size > data.size() - pos - 8)
          return makeError(ErrorCode::ChunkTruncated,
                            std::format("ADT chunk {} at {:#x} overruns the file", fourccToString(magic), pos));
        const auto payload = data.subspan(pos + 8, size);
        pos += 8 + size;

        // MCNK is the one non-reflective chunk: 256 repeats, each a per-file
        // portion merged into a MapChunk via readFrom.
        if (magic == fourCc("MCNK")) {
          if (chunks.size() < chunkIndex + 1) chunks.resize(chunkIndex + 1);
          if (auto r = chunks[chunkIndex].readFrom(payload, kind, alphaFormat); !r) return r;
          ++chunkIndex;
          continue;
        }

        // Every other chunk routes to its `chunk()`-annotated member by fourcc.
        // Version-gated members live in trait bases: for a version where a trait
        // is inactive the member is absent from `members`, so an unmodeled chunk
        // (e.g. MCIN, whose fields are derived and needs no member) simply does
        // not match and is skipped.
        Result<void> outcome{};
        bool matched = false;
        template for (constexpr auto m : Members) {
          if constexpr (constexpr auto spec = formats::detail::annotation<formats::detail::ChunkSpec, m>(); spec.
            has_value()) {
            if (!matched && magic == spec->magic) {
              matched = true;
              outcome = formats::detail::readValue(this->[:m:], payload, magic, pos, spec->endian);
              // the two members that need a post-read fix-up: MHDR's derived
              // offsets are zeroed, and MDID flips the texture-scheme flag.
              if constexpr (std::meta::identifier_of(m) == "header") _normalizeMhdr();
              else if constexpr (std::meta::identifier_of(m) == "diffuseTextureIds")
                if constexpr (requires { this->usesTextureFdids; }) this->usesTextureFdids = true;
            }
          }
        }
        if (!outcome) return outcome;
      }
      return {};
    }

    template <ClientVersion V>
    Result<FileBuffer> ADT<V>::writeFile(FileKind kind, AlphaFormat alpha) const {
      using Self = ADT<V>;
      static constexpr auto Members = formats::detail::membersOf<Self>();
      const bool mono = kind == FileKind::Monolithic;

      FileBuffer out;
      std::size_t mhdrAt = 0, mcinAt = 0;
      std::array<std::pair<std::size_t, std::size_t>, 256> mcnkLoc{};
      // (fourcc pos, payload size)
      // Emitted chunk fourcc positions, for the MHDR offset table.
      std::array<std::pair<std::uint32_t, std::size_t>, ChunkOrder.size()> emitted{};
      std::size_t nEmitted = 0;
      std::optional<Error> err;

      const auto record = [&](std::uint32_t magic, std::size_t at) {
        emitted[nEmitted++] = {magic, at};
      };
      const auto positionOf = [&](std::uint32_t magic) -> std::size_t {
        for (std::size_t i = 0; i < nEmitted; ++i)
          if (emitted[i].first == magic) return emitted[i].second;
        return 0;
      };

      for (const std::uint32_t want : ChunkOrder) {
        if (want == fourCc("MVER")) {
          _emitChunk(out, want, [&] {
            const std::uint32_t v = AdtVersion18;
            _put(out, &v, 4);
          });
          continue;
        }
        if (want == fourCc("MCIN")) {
          if (mono)
            mcinAt = _emitChunk(out, want, [&] {
              out.insert(out.end(), 256 * 16, std::byte{0});
            });
          continue;
        }
        if (want == fourCc("MCNK")) {
          for (std::size_t i = 0; i < chunks.size() && i < 256; ++i) {
            const std::size_t at = _emitChunk(out, want, [&] {
              if (auto r = chunks[i].writeTo(out, kind, alpha); !r) err = r.error();
            });
            mcnkLoc[i] = {at, out.size() - (at + 8)};
          }
          continue;
        }

        // Reflective emit: the member whose chunk() magic == want, if it is routed
        // to this file and engaged.
        template for (constexpr auto m : Members) {
          if constexpr (constexpr auto spec = formats::detail::annotation<formats::detail::ChunkSpec, m>(); spec.
            has_value()) {
            if constexpr (constexpr auto route = formats::detail::annotation<InFileSpec, m>(); route.has_value()) {
              if (spec->magic == want && routesTo(route->file, kind) && _writeEngaged<m>()) {
                // Splice the member reference HERE (m is a constant expression);
                // inside the lambda m is captured by reference and would not be.
                const auto& member = this->[:m:];
                const std::size_t at = _emitChunk(out, want, [&] {
                  if (auto r = formats::detail::writeValue(member, out); !r) err = r.error();
                });
                record(want, at);
                if (want == fourCc("MHDR")) mhdrAt = at;
              }
            }
          }
        }
      }
      if (err) return std::unexpected{*err};

      // Stamp the derived tables. MCIN: absolute offset at each MCNK fourcc, size
      // including the 8-byte header (monolithic only). MHDR: offsets relative to
      // the MHDR data start, pointing at each target chunk's fourcc.
      if (mono)
        for (std::size_t i = 0; i < 256; ++i) {
          const std::uint32_t entry[4]{
            static_cast<std::uint32_t>(mcnkLoc[i].first),
            static_cast<std::uint32_t>(mcnkLoc[i].second + 8),
            0,
            0
          };
          std::memcpy(out.data() + mcinAt + 8 + i * 16, entry, 16);
        }
      if (fileHasHeader(kind)) {
        const std::size_t base = mhdrAt + 8;
        const auto rel = [&](std::uint32_t magic) {
          const std::size_t at = positionOf(magic);
          return at == 0 ? 0u : static_cast<std::uint32_t>(at - base);
        };
        MHDRData h = header;
        h.ofsMcin = mcinAt == 0 ? 0u : static_cast<std::uint32_t>(mcinAt - base);
        h.ofsMtex = rel(fourCc("MTEX"));
        h.ofsMmdx = rel(fourCc("MMDX"));
        h.ofsMmid = rel(fourCc("MMID"));
        h.ofsMwmo = rel(fourCc("MWMO"));
        h.ofsMwid = rel(fourCc("MWID"));
        h.ofsMddf = rel(fourCc("MDDF"));
        h.ofsModf = rel(fourCc("MODF"));
        h.ofsMfbo = rel(fourCc("MFBO"));
        h.ofsMh2O = rel(fourCc("MH2O"));
        h.ofsMtxf = rel(fourCc("MTXF"));
        std::memcpy(out.data() + mhdrAt + 8, &h, sizeof(MHDRData));
      }
      return out;
    }

    template <ClientVersion V>
    Result<void> ADT<V>::read(fs::FileSystem& fs, const FileKey& key, AlphaFormat alpha) {
      *this = ADT{};
      alphaFormat = alpha;

      if constexpr (V < builds::Cata) {
        // The pre-Cata tile is a single monolithic .adt carrying every chunk.
        return fs.readFile(key).and_then([&](FileBuffer data) -> Result<void> {
          chunks.assign(256, adt::MapChunk<V>{});
          if (auto r = parseFile(data, FileKind::Monolithic); !r) return r;
          normalizeChunks();
          return {};
        });
      }
      else {
        // Cata+ split tile: the root .adt plus its _tex0/_obj0/_obj1/_lod split
        // files, located by the "{stem}_<suffix>.adt" naming convention.
        // root/tex0/obj0 are parsed and MERGED into the one entity (their 256
        // MCNK streams accumulate per chunk); _obj1/_lod are preserved verbatim
        // this stage (structured later).
        const FileKey resolved = fs.resolve(key);
        if (!resolved.path)
          return makeError(ErrorCode::PathNotResolvable, "loading a split ADT needs the root file path");
        std::string_view stem = *resolved.path;
        if (stem.ends_with(".adt")) stem.remove_suffix(4);
        const auto sibling = [&](std::string_view suffix) {
          return std::format("{}{}.adt", stem, suffix);
        };

        const auto rootData = fs.readFile(key);
        if (!rootData) return std::unexpected{rootData.error()};
        chunks.assign(256, adt::MapChunk<V>{});
        if (auto r = parseFile(*rootData, FileKind::Root); !r) return r;

        const auto load = [&](std::string_view suffix, FileKind fk) -> Result<void> {
          const FileKey k{sibling(suffix)};
          if (!fs.exists(k)) return {};
          const auto data = fs.readFile(k);
          if (!data)
            return makeError(data.error().code, std::format("{} split file: {}", suffix, data.error().message));
          return parseFile(*data, fk);
        };
        if (auto r = load("_tex0", FileKind::Tex0); !r) return r;
        if (auto r = load("_obj0", FileKind::Obj0); !r) return r;

        // preserve the unmodeled split files verbatim
        if constexpr (requires { this->obj1Data; }) {
          const auto keep = [&](std::string_view suffix, std::vector<std::byte>& into) {
            const FileKey k{sibling(suffix)};
            if (fs.exists(k))
              if (const auto data = fs.readFile(k)) into = *data;
          };
          keep("_obj1", this->obj1Data);
          keep("_lod", this->lodData);
        }

        normalizeChunks();
        return {};
      }
    }

    template <ClientVersion V>
    Result<void> ADT<V>::write(fs::FileSystem& fs, const FileKey& key, AlphaFormat alpha) const {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return makeError(ErrorCode::PathNotResolvable, "saving an ADT needs a path");

      // addFile returns Result<FileDataID>; a save only cares whether it failed.
      const auto add = [&](std::string_view path, std::span<const std::byte> bytes) -> Result<void> {
        return fs.addFile(path, bytes).transform([](auto&&) {});
      };

      if constexpr (V < builds::Cata) {
        return writeFile(FileKind::Monolithic, alpha).and_then([&](FileBuffer data) {
          return add(*resolved.path, data);
        });
      }
      else {
        std::string_view stem = *resolved.path;
        if (stem.ends_with(".adt")) stem.remove_suffix(4);
        const auto sibling = [&](std::string_view suffix) {
          return std::format("{}{}.adt", stem, suffix);
        };
        const auto store = [&](FileKind fk, std::string_view suffix) -> Result<void> {
          return writeFile(fk, alpha).and_then([&](FileBuffer data) { return add(sibling(suffix), data); });
        };
        // the root file keeps the bare "{stem}.adt" name
        if (auto r = writeFile(FileKind::Root, alpha).and_then([&](FileBuffer data) {
          return add(*resolved.path, data);
        }); !r) return r;
        if (auto r = store(FileKind::Tex0, "_tex0"); !r) return r;
        if (auto r = store(FileKind::Obj0, "_obj0"); !r) return r;
        if constexpr (requires { this->obj1Data; }) {
          if (!this->obj1Data.empty())
            if (auto r = add(sibling("_obj1"), this->obj1Data); !r) return r;
          if (!this->lodData.empty())
            if (auto r = add(sibling("_lod"), this->lodData); !r) return r;
        }
        return {};
      }
    }
  }
}
