#pragma once

/** @file
    The per-chunk terrain entity (namespace wowlib::formats::adt): MapChunk<V>,
    one of the 256 MCNK terrain chunks of a tile, fully decoded.

    MapChunk is NOT a generic ChunkedFile: the MCNK payload has a 128-byte header
    of derived offsets/sizes, sub-chunks whose lengths the header (not their own
    size field) governs (MCAL/MCLQ/MCSH), a pre-Cata MCNR trailer of 13 undeclared
    padding bytes, and alpha maps in three on-disk encodings. Because a Cata+ tile
    splits one terrain chunk across the root, _tex0 and _obj0 split ADT files, the
    reader/writer work on one physical FILE at a time (read_from / write_to),
    accumulating into the same MapChunk across files; a pre-Cata tile is one
    monolithic file carrying every sub-chunk.

    Its sub-chunk members still carry the same `chunk()` + `inFile()` annotations
    the tile-level ADT chunks do, so read_from/write_to are REFLECTION-DRIVEN: a
    `template for` over the members matches the fourcc and routes by physical file.
    The UNIFORM sub-chunks (plain vectors / the SelfSerializing MCLQ) transfer
    through the chunk engine's readValue/writeValue; the ones that break the
    member mapping (MCNR padding, MCLY, MCAL codec, MCSH codec) carry a
    `serializedBy<Codec>()` annotation naming a MapChunk-context codec. MCRF (one
    chunk, two members) and the derived 128-byte header are the only hand-written
    cases — the MCNK analogue of ADT's MCNK/MHDR.

    Version-gated members live in conditionally-inherited trait bases (adt::detail):
    vertex colors since WotLK, vertex lighting / terrain materials since Cata, the
    legacy MCLQ liquid until Cata (it survives through WotLK — Outland tiles still
    ship it). */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/adt/boundaries.hpp>
#include <wowlib/formats/adt/chunks/header.hpp>
#include <wowlib/formats/adt/chunks/liquid.hpp>
#include <wowlib/formats/adt/chunks/object.hpp>
#include <wowlib/formats/adt/chunks/texture.hpp>
#include <wowlib/formats/adt/codec.hpp>
#include <wowlib/formats/adt/liquid.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/common/version_range.hpp>
#include <wowlib/formats/common/version_slot.hpp>

namespace wowlib::formats::adt {
  using namespace wowlib::formats::adt::chunks;

  /** Which physical ADT file a chunk's portion belongs to. Pre-Cata tiles are one
      `monolithic` file carrying every chunk; Cata+ tiles distribute chunks over
      the split ADT files (root/_tex0/_obj0/_obj1/_lod). */
  enum class [[
      =welder::weld,
      =welder::doc(R"(
        The physical ADT file a chunk belongs to. Pre-Cataclysm tiles are a single
        `monolithic` .adt; Cataclysm split the tile into a `root` .adt and the
        `tex0` / `obj0` / `obj1` / `lod` split files. wowlib presents one unified
        ADT regardless; this only surfaces in the low-level split-file API.)")
    ]] FileKind : std::uint8_t {
    Monolithic [[=welder::doc("The single pre-Cataclysm .adt (every chunk).")]] = 0,
    Root [[=welder::doc(
      "The Cata+ root .adt (terrain heights/normals/colors, liquid, "
      "sounds).")]] = 1,
    Tex0 [[=welder::doc(
      "The Cata+ _tex0.adt (texture layers, alpha/shadow maps, "
      "materials).")]] = 2,
    Obj0 [[=welder::doc("The Cata+ _obj0.adt (doodad/object references and "
      "placements).")]] = 3,
    Obj1 [[=
      welder::doc("The Cata+ _obj1.adt (level-of-detail object placements).")]]
    = 4,
    Lod [[=welder::doc(
      "The Legion+ _lod.adt (low-detail geometry and liquids).")]] = 5
  };

  /** The physical-file group a binary chunk (tile-level or MCNK sub-chunk) is routed
      to on write; the monolithic file carries every group. Used by both ADT tile
      chunks and MapChunk sub-chunks via the `inFile()` annotation. */
  enum class InFile : std::uint8_t {
    Root, /**< The root .adt (header, terrain, water, sounds, flying bounds). */
    Tex, /**< The _tex0.adt (textures, layers, alpha/shadow maps, materials). */
    Obj /**< The _obj0.adt (model/WMO names, placements, references). */
  };

  namespace detail {
    /** Stored form of an `inFile` annotation: a binary chunk's physical-file group. */
    struct InFileSpec {
      InFile file;
    };

    /** Stored form of a `serializedBy` annotation: the reflection of a codec type
        handling a sub-chunk that a plain readValue/writeValue cannot (external
        length, a transform, or a companion member). */
    struct SerializerSpec {
      std::meta::info codec;
    };
  }

  /** Annotate a binary chunk member with the physical file it is routed to on write
      (the monolithic file carries every group).
      @param file the physical-file group.
      @return the annotation payload. */
  consteval detail::InFileSpec inFile(InFile file) { return {file}; }

  /** Annotate a sub-chunk member with a MapChunk-context codec (a struct with
      static `read(self, span, ReadCtx)` / `write(self, out, WriteCtx)` /
      `engaged(self, WriteCtx)` templates) instead of the default uniform transfer.
      @param codec the reflection of the codec type, e.g. `^^AlphaCodec`.
      @return the annotation payload. */
  consteval detail::SerializerSpec serializedBy(std::meta::info codec) {
    return {codec};
  }

  /** Whether a member routed to @a file participates in physical file @a kind: the
      monolithic file carries every group, each split file only its own.
      @param file the member's routing group.
      @param kind the physical file being read/written.
      @return whether the member participates. */
  constexpr bool routesTo(InFile file, FileKind kind) {
    switch (kind) {
    case FileKind::Monolithic: return true;
    case FileKind::Root: return file == InFile::Root;
    case FileKind::Tex0: return file == InFile::Tex;
    case FileKind::Obj0: return file == InFile::Obj;
    default: return false;
    }
  }

  /** Whether a given physical file carries the 128-byte MCNK header (the root or
      the pre-Cata monolithic file).
      @param kind the physical file.
      @return true for the monolithic and root files. */
  constexpr bool fileHasHeader(FileKind kind) {
    return kind == FileKind::Monolithic || kind == FileKind::Root;
  }

  /** The version-agnostic base of every MapChunk<V> (welded as "MapChunk"): the
      language bindings attach for_version here and give the per-version chunks a
      common welded supertype. No role in the C++ API. */
  struct [[
      =welder::weld,
      =welder::weld_as("MapChunk"),
      WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A terrain chunk (MCNK), abstract over the client version. Usually obtained
        from ADT.chunks rather than constructed; the per-version MapChunk* classes
        are subclasses. Construct a concrete version with
        MapChunk.for_version(expansion). See https://wowdev.wiki/ADT/v18#MCNK_chunk.)")
    ]] MapChunkBase {};

  namespace detail {
    /** The 9x9 + 8x8 = 145 vertices of the MCVT height / MCNR normal grid. */
    inline constexpr std::size_t McvtCount = 145;

    /** Context a sub-chunk codec needs to read one MCNK sub-chunk: the (already
        parsed) header, the sibling texture layers (MCAL indexes them), the tile
        alpha bit depth and whether to repair the 63x63 "unfixed" edge form. */
    struct MapChunkReadCtx {
      const SMChunk& header;
      const std::vector<SMLayer>& layers;
      AlphaFormat af;
      bool fix;
      FileKind kind;
    };

    /** Context a sub-chunk codec needs to write one MCNK sub-chunk. The alpha
        layout is pre-built once (MCLY must emit each layer's offsetInMcal, which
        MCAL computes) into these two fields; the layer/alpha codecs read them. */
    struct MapChunkWriteCtx {
      const std::vector<SMLayer>& stampedLayers;
      // layers with offsetInMcal set
      std::span<const std::byte> alphaBlob; // the pre-encoded MCAL blob
    };

    /** Append @a n raw bytes at @a p to @a out (a byte-copy primitive). */
    inline void appendBytes(FileBuffer& out, const void* p, std::size_t n) {
      const auto* b = static_cast<const std::byte*>(p);
      out.insert(out.end(), b, b + n);
    }

    /** MCNR codec: 145 XZY normal triples followed by 13 undeclared padding bytes
        (a near-constant client pattern preserved for the semantic round-trip). */
    struct NormalCodec {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub, const MapChunkReadCtx&) {
        self.normals.resize(McvtCount);
        std::memcpy(self.normals.data(), sub.data(), std::min(sub.size(), McvtCount * 3));
        if (sub.size() >= McvtCount * 3 + 13)
          std::memcpy(self.mcnrPadding.data(), sub.data() + McvtCount * 3, 13);
        return {};
      }

      template <typename Chunk>
      static void write(const Chunk& self, FileBuffer& out, const MapChunkWriteCtx&) {
        appendBytes(out, self.normals.data(), self.normals.size() * 3);
        appendBytes(out, self.mcnrPadding.data(), 13);
      }

      template <typename Chunk>
      static bool engaged(const Chunk& self, const MapChunkWriteCtx&) {
        return !self.normals.empty();
      }
    };

    /** MCLY codec: the texture layers, plus sizing alphaMaps to the layer count
        (MCAL may be absent on Cata+ when empty, so alphaMaps must be sized here
        to keep it aligned with layers). */
    struct LayerCodec {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub, const MapChunkReadCtx&) {
        self.layers.resize(sub.size() / 16);
        std::memcpy(self.layers.data(), sub.data(), self.layers.size() * 16);
        self.alphaMaps.assign(self.layers.size(), {});
        return {};
      }

      template <typename Chunk>
      static void write(const Chunk&, FileBuffer& out, const MapChunkWriteCtx& ctx) {
        appendBytes(out, ctx.stampedLayers.data(), ctx.stampedLayers.size() * 16);
      }

      template <typename Chunk>
      static bool engaged(const Chunk& self, const MapChunkWriteCtx&) {
        return !self.layers.empty();
      }
    };

    /** MCAL codec: decode/encode the per-layer 64x64 alpha maps through
        AlphaMapCodec, indexing the sibling layers' flags + offsetInMcal. */
    struct AlphaCodec {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub, const MapChunkReadCtx& ctx) {
        if (self.alphaMaps.size() != self.layers.size()) self.alphaMaps.resize(self.layers.size());
        const AlphaMapCodec codec{ctx.af};
        for (auto&& [layer, out_map] : std::views::zip(self.layers, self.alphaMaps)) {
          if (!hasFlag(layer.flags, LayerFlags::UseAlphaMap)) continue;
          if (layer.offsetInMcal > sub.size()) continue;
          const bool compressed = hasFlag(layer.flags, LayerFlags::AlphaMapCompressed);
          out_map = codec.decode(sub.subspan(layer.offsetInMcal), compressed, ctx.fix);
        }
        return {};
      }

      /** Build the MCAL blob and the layers copy carrying each layer's derived
          offsetInMcal (MCLY emits that copy). Called once before the write walk.
          @param self    the chunk.
          @param af      the tile alpha bit depth.
          @param blob    receives the encoded MCAL bytes.
          @param stamped receives the layers with offsetInMcal stamped. */
      template <typename Chunk>
      static void prepare(const Chunk& self,
                          AlphaFormat af,
                          std::vector<std::byte>& blob,
                          std::vector<SMLayer>& stamped) {
        stamped = self.layers;
        const AlphaMapCodec codec{af};
        for (std::size_t i = 0; i < stamped.size(); ++i) {
          const bool has = i < self.alphaMaps.size() && !self.alphaMaps[i].empty() && hasFlag(
            stamped[i].flags, LayerFlags::UseAlphaMap);
          stamped[i].offsetInMcal = static_cast<std::uint32_t>(blob.size());
          if (!has) continue;
          codec.encode(self.alphaMaps[i], hasFlag(stamped[i].flags, LayerFlags::AlphaMapCompressed), blob);
        }
      }

      template <typename Chunk>
      static void write(const Chunk&, FileBuffer& out, const MapChunkWriteCtx& ctx) {
        appendBytes(out, ctx.alphaBlob.data(), ctx.alphaBlob.size());
      }

      template <typename Chunk>
      static bool engaged(const Chunk&, const MapChunkWriteCtx& ctx) {
        // MCAL is omitted when no layer carries a map (most chunks): emitting an
        // empty one would round-trip a no-MCAL chunk to a 1-entry alphaMaps.
        return !ctx.alphaBlob.empty();
      }
    };

    /** MCSH codec: the 64x64 shadow map through ShadowMapCodec. */
    struct ShadowCodec {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub, const MapChunkReadCtx& ctx) {
        self.shadowMap = ShadowMapCodec{}.decode(sub, ctx.fix);
        return {};
      }

      template <typename Chunk>
      static void write(const Chunk& self, FileBuffer& out, const MapChunkWriteCtx&) {
        std::vector<std::byte> packed;
        ShadowMapCodec{}.encode(self.shadowMap, packed);
        appendBytes(out, packed.data(), packed.size());
      }

      template <typename Chunk>
      static bool engaged(const Chunk& self, const MapChunkWriteCtx&) {
        return !self.shadowMap.empty();
      }
    };

    /** MCSE codec: typed emitter records — the era picks the entry
        (pre-WotLK 52-byte CWSoundEmitterVanilla, 28-byte CWSoundEmitter
        after; the member's value_type carries the choice). A payload that is
        not a whole number of entries is preserved verbatim in mcseRaw. */
    struct SoundEmitterCodec {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub, const MapChunkReadCtx&) {
        using Entry = std::remove_cvref_t<decltype(self.soundEmitters )>::value_type;
        if (sub.size() % sizeof(Entry) == 0) {
          self.soundEmitters.resize(sub.size() / sizeof(Entry));
          std::memcpy(self.soundEmitters.data(), sub.data(), sub.size());
          return {};
        }
        return self.mcseRaw.read(sub);
      }

      template <typename Chunk>
      static void write(const Chunk& self, FileBuffer& out, const MapChunkWriteCtx&) {
        using Entry = std::remove_cvref_t<decltype(self.soundEmitters )>::value_type;
        if (!self.mcseRaw.empty()) {
          (void)self.mcseRaw.write(out);
          return;
        }
        appendBytes(out, self.soundEmitters.data(), self.soundEmitters.size() * sizeof(Entry));
      }

      template <typename Chunk>
      static bool engaged(const Chunk& self, const MapChunkWriteCtx&) {
        return !self.soundEmitters.empty() || !self.mcseRaw.empty();
      }
    };

    /** Vertex colors (MCCV), WotLK+. */
    struct MapChunkColor {
      [[=chunk("MCCV"),
        =inFile(InFile::Root),
        =formats::countExactly(McvtCount),
        =welder::doc(
          "Per-vertex colors (MCCV, WotLK+): 145 BGRA entries blended onto the "
          "terrain (0x7F = neutral)."),
        =welder::mark::no_reassign]]
      std::vector<CImVector> vertexColors;

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkColor&) const = default;
    };

    /** Vertex lighting (MCLV) and terrain materials (MCMT), Cata+. */
    struct MapChunkCata {
      [[=chunk("MCLV"),
        =inFile(InFile::Root),
        =formats::countExactly(McvtCount),
        =welder::doc(
          "Per-vertex baked lighting (MCLV, Cata+): 145 ARGB entries from "
          "level-designer omni lights."),
        =welder::mark::no_reassign]]
      std::vector<CArgb> vertexLighting;

      [[=chunk("MCMT"),
        =inFile(InFile::Tex),
        =welder::doc("Per-layer terrain material ids (MCMT, Cata+)."),
        =welder::mark::no_reassign]]
      std::vector<SMTerrainMaterial> materialIds;

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkCata&) const = default;
    };

    /** Legacy liquid (MCLQ). Available through WotLK and removed at Cata (MH2O
        supersedes it, but Outland tiles keep shipping MCLQ in WotLK clients). */
    struct MapChunkLegacyLiquid {
      [[=chunk("MCLQ"),
        =inFile(InFile::Root),
        =welder::doc(
          "The legacy per-chunk liquid (MCLQ, up to and including WotLK).")]]
      MCLQData legacyLiquid{};

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkLegacyLiquid&) const = default;
    };
  }

  namespace detail {
    /** A terrain chunk (MCNK) for one client version. Instantiate through the
        canonicalizing adt::MapChunk alias. */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          One terrain chunk (MCNK) of a map tile, fully decoded: the chunk header, the
          9x9+8x8 height and normal grids, the texture layers with their decoded
          64x64 alpha maps, the shadow map, doodad/object references, sound emitters
          and (version dependent) vertex colors, baked lighting and legacy liquid.
          A tile has 256 of these. wowlib decodes alpha/shadow maps to a plain 64x64
          edit surface and re-encodes on write; the round-trip is semantic, not
          byte-identical. See https://wowdev.wiki/ADT/v18#MCNK_chunk.)")
      ]] MapChunk
      : MapChunkBase,
        Slot<V, builds::WotLK, MapChunkColor>,
        Slot<V, builds::Cata, MapChunkCata>,
        Slot<V, ClientVersion{0, 0, 0, 0}, MapChunkLegacyLiquid, builds::Cata> {
      static constexpr ClientVersion Version = V;

      /** The canonical sub-chunk write order (excluding the references, which are
          the MCRF-vs-MCRD/MCRW special case, emitted last). Each physical file
          emits the subset routed to it, in this order — see write_to. */
      static constexpr std::array SubChunkOrder{
        fourCc("MCVT"),
        fourCc("MCCV"),
        fourCc("MCLV"),
        fourCc("MCNR"),
        fourCc("MCLQ"),
        fourCc("MCSE"),
        fourCc("MCLY"),
        fourCc("MCSH"),
        fourCc("MCAL"),
        fourCc("MCMT")
      };

      [[=welder::doc(
        "The chunk header (flags, grid position, area, holes, origin).")]]
      SMChunk header{};

      [[=chunk("MCVT"),
        =inFile(InFile::Root),
        =formats::countExactly(detail::McvtCount),
        =welder::doc(
          "The 9x9 + 8x8 = 145 terrain heights (MCVT), relative to the chunk "
          "origin, in the interleaved outer/inner row order."),
        =welder::mark::no_reassign]]
      std::vector<float> heights;

      [[=chunk("MCNR"),
        =inFile(InFile::Root),
        =serializedBy(^^detail::NormalCodec),
        =formats::countExactly(detail::McvtCount),
        =welder::doc("The 145 terrain normals (MCNR), X Z Y signed bytes."),
        =welder::mark::no_reassign]]
      std::vector<MCNREntry> normals;

      [[=chunk("MCLY"),
        =inFile(InFile::Tex),
        =serializedBy(^^detail::LayerCodec),
        =welder::doc(
          "The texture layers (MCLY); layer 0 is opaque, later layers blend "
          "through their alpha map."),
        =welder::mark::no_reassign]]
      std::vector<SMLayer> layers;

      [[=chunk("MCAL"),
        =inFile(InFile::Tex),
        =serializedBy(^^detail::AlphaCodec),
        =formats::countMatches("layers"),
        =welder::doc(
          "One decoded 64x64 (4096-byte) alpha map per layer, aligned with "
          "layers (layer 0's is empty); 0 = base texture, 255 = this layer."),
        =welder::mark::no_reassign]]
      std::vector<std::vector<std::uint8_t>> alphaMaps;

      [[=chunk("MCSH"),
        =inFile(InFile::Tex),
        =serializedBy(^^detail::ShadowCodec),
        =formats::countExactly(detail::AlphaTexels),
        =welder::doc(
          "The decoded 64x64 (4096-byte) shadow map (MCSH), 0/1 per texel; "
          "empty when the chunk casts no baked shadow."),
        =welder::mark::no_reassign]]
      std::vector<std::uint8_t> shadowMap;

      [[=chunk("MCRD"),
        =inFile(InFile::Obj),
        =formats::indexesInRoot("doodadPlacements"),
        =welder::doc(
          "Doodad references (MCRF doodad part pre-Cata, MCRD Cata+): indices "
          "into the tile's MDDF placements drawn in this chunk."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> doodadRefs;

      [[=chunk("MCRW"),
        =inFile(InFile::Obj),
        =formats::indexesInRoot("wmoPlacements"),
        =welder::doc(
          "Object references (MCRF object part pre-Cata, MCRW Cata+): indices "
          "into the tile's MODF placements drawn in this chunk."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> objectRefs;

      [[=chunk("MCSE"),
        =inFile(InFile::Root),
        =serializedBy(^^detail::SoundEmitterCodec),
        =welder::doc("Sound emitters placed in this chunk (MCSE): pre-WotLK "
          "versions carry the full 52-byte inline emitter "
          "(CWSoundEmitterVanilla), WotLK+ the 28-byte "
          "SoundEntriesAdvanced reference (CWSoundEmitter)."),
        =welder::mark::no_reassign]]
      std::vector<std::conditional_t<(V < builds::WotLK), CWSoundEmitterVanilla, CWSoundEmitter>> soundEmitters;

      /** A malformed MCSE payload (not a whole number of entries), preserved
          verbatim — see detail::SoundEmitterCodec; empty whenever
          soundEmitters parsed. */
      [[=welder::mark::exclude]]
      ChunkBlob mcseRaw;

      /** The 13 undeclared trailing bytes after the MCNR normals (a near-constant
          client pattern, not derived from the normals); preserved for the
          semantic round-trip. Read/written as part of MCNR by NormalCodec. */
      [[=welder::mark::exclude]]
      std::array<std::uint8_t, 13> mcnrPadding{};

      /** Validation hook (see formats::detail::validateValue): the terrain
          chunk's contracts the annotations cannot express — the decoded alpha
          surfaces' fixed size and the layer/alpha relationship. Contracts
          reaching into the TILE (layer texture ids, doodad and object
          references) belong to the ADT's validate(), which is where the
          texture and placement tables live.
          @param report the report findings land in. */
      [[=welder::mark::exclude]]
      void validateExtra(ValidationReport& report) const {
        // wowlib always holds alpha maps decoded to the full 64x64 edit
        // surface, whatever encoding they had on disk; layer 0 is opaque and
        // carries none
        for (std::size_t i = 0; i < alphaMaps.size(); ++i)
          if (!alphaMaps[i].empty() && alphaMaps[i].size() != detail::AlphaTexels)
            report.addError(std::format("alphaMaps[{}]", i),
                             std::format("decoded alpha map holds {} texels, not {}", alphaMaps[i].size(),
                                         detail::AlphaTexels));
        if (!alphaMaps.empty() && !alphaMaps[0].empty())
          report.addWarning("alphaMaps[0]", "layer 0 is the opaque base layer; its alpha map is ignored");
      }

      // --- physical-file serialization (definitions below) -------------------

      /** Decode one physical file's portion of this terrain chunk into the
          accumulating entity, reflecting over the `chunk()`-annotated members.
          @param payload the MCNK chunk payload from this file.
          @param kind    which physical file the portion came from.
          @param af      the tile's alpha-map bit depth (supplied externally).
          @return a structural error or success. */
      [[=welder::mark::exclude]]
      Result<void> read_from(std::span<const std::byte> payload, FileKind kind, AlphaFormat af);

      /** Append this chunk's portion for @a kind to @a out (the MCNK payload; the
          caller wraps the fourcc+size). Header-bearing files emit the 128-byte
          header with every offset/size/count field freshly stamped.
          @param out  the destination buffer.
          @param kind which physical file's portion to write.
          @param af   the tile's alpha-map bit depth (supplied externally).
          @return a structural error or success. */
      [[=welder::mark::exclude]]
      Result<void> write_to(FileBuffer& out, FileKind kind, AlphaFormat af) const;

      [[=welder::mark::exclude]]
      bool operator==(const MapChunk&) const = default;

    private:
      /** One emitted sub-chunk's position, for stamping the derived header. */
      struct Emitted {
        std::uint32_t magic = 0;
        std::uint32_t offset = 0; // chunk-start-relative (points at the fourcc)
        std::uint32_t total = 0;
        // fourcc + size + payload, for the size_* fields
      };

      /** Emit one sub-chunk (fourcc + size + body) into @a out and return its
          MCNK-chunk-start-relative offset (8 + the payload-local offset of its
          fourcc).
          @tparam Body the payload-writing callable type.
          @param out   the destination buffer.
          @param base  the MCNK payload start (the SMChunk header) in @a out.
          @param magic the sub-chunk fourcc.
          @param body  writes the sub-chunk payload into @a out.
          @return the sub-chunk's offset relative to the MCNK chunk start. */
      template <typename Body>
      static std::uint32_t _emitSubchunk(FileBuffer& out, std::size_t base, std::uint32_t magic, Body&& body) {
        const auto ofs = static_cast<std::uint32_t>(out.size() - base) + 8;
        appendBytes(out, &magic, 4);
        const std::size_t sizeAt = out.size();
        out.insert(out.end(), 4, std::byte{0});
        body();
        const auto size = static_cast<std::uint32_t>(out.size() - sizeAt - 4);
        std::memcpy(out.data() + sizeAt, &size, 4);
        return ofs;
      }

      /** The MCRF/MCRD/MCRW references (the one sub-chunk that maps to two members
          and changes shape by era): pre-Cata writes the combined MCRF, split files
          write MCRD + MCRW. Records what it emitted for header stamping.
          @param out      the destination buffer.
          @param base     the MCNK payload start in @a out.
          @param kind     the physical file.
          @param emitted  the emitted-sub-chunk table to append to.
          @param n        the emitted count to advance. */
      void _write_refs(FileBuffer& out,
                       std::size_t base,
                       FileKind kind,
                       std::span<Emitted> emitted,
                       std::size_t& n) const {
        const auto emit = [&](std::uint32_t magic, auto&& body) {
          const std::size_t before = out.size();
          const std::uint32_t ofs = _emitSubchunk(out, base, magic, body);
          emitted[n++] = {magic, ofs, static_cast<std::uint32_t>(out.size() - before)};
        };
        if (kind != FileKind::Monolithic) {
          if (!doodadRefs.empty())
            emit(fourCc("MCRD"), [&] {
              appendBytes(out, doodadRefs.data(), doodadRefs.size() * 4);
            });
          if (!objectRefs.empty())
            emit(fourCc("MCRW"), [&] {
              appendBytes(out, objectRefs.data(), objectRefs.size() * 4);
            });
        }
        else if (!doodadRefs.empty() || !objectRefs.empty()) {
          emit(fourCc("MCRF"), [&] {
            appendBytes(out, doodadRefs.data(), doodadRefs.size() * 4);
            appendBytes(out, objectRefs.data(), objectRefs.size() * 4);
          });
        }
      }

      /** Split the combined pre-Cata MCRF list into doodad and object refs (the
          header's nDoodadRefs marks the boundary).
          @param sub  the MCRF data (u32 indices).
          @param kind the physical file (the header count is only trusted with one). */
      void _read_combined_refs(std::span<const std::byte> sub, FileKind kind) {
        const std::size_t total = sub.size() / 4;
        const std::size_t nDd = std::min<std::size_t>(fileHasHeader(kind) ? header.nDoodadRefs : 0, total);
        doodadRefs.resize(nDd);
        objectRefs.resize(total - nDd);
        std::memcpy(doodadRefs.data(), sub.data(), nDd * 4);
        std::memcpy(objectRefs.data(), sub.data() + nDd * 4, (total - nDd) * 4);
      }

      /** Stamp the derived MCNK header fields from the sub-chunks emitted here;
          fields not emitted keep their read value (so a split root preserves the
          tex/obj-owned fields). The do_not_fix_alpha flag is left as-is: the ADT
          reader normalizes it after every portion is in, so it is already correct.
          @param out     the destination buffer (the header sits at @a base).
          @param base    the MCNK payload start in @a out.
          @param emitted the sub-chunks emitted into this file. */
      void _stamp_header(FileBuffer& out, std::size_t base, std::span<const Emitted> emitted) const {
        SMChunk h;
        std::memcpy(&h, out.data() + base, sizeof(SMChunk));
        const bool highRes = hasFlag(h.flags, MapChunkFlags::HighResHoles);
        const auto ofs = [&](std::uint32_t magic) -> std::optional<std::uint32_t> {
          for (const Emitted& e : emitted)
            if (e.magic == magic) return e.offset;
          return std::nullopt;
        };
        const auto sz = [&](std::uint32_t magic) -> std::uint32_t {
          for (const Emitted& e : emitted)
            if (e.magic == magic) return e.total;
          return 0;
        };

        if (auto o = ofs(fourCc("MCVT")); o && !highRes) h.ofsHeight = *o;
        if (auto o = ofs(fourCc("MCNR")); o && !highRes) h.ofsNormal = *o;
        if (auto o = ofs(fourCc("MCCV")); o) h.ofsMccv = *o;
        if (auto o = ofs(fourCc("MCLV")); o) h.ofsMclv = *o;
        if (auto o = ofs(fourCc("MCLY")); o) {
          h.ofsLayer = *o;
          h.nLayers = static_cast<std::uint32_t>(layers.size());
        }
        if (auto o = ofs(fourCc("MCAL")); o) {
          h.ofsAlpha = *o;
          h.sizeAlpha = sz(fourCc("MCAL"));
        }
        if (auto o = ofs(fourCc("MCSH")); o) {
          h.ofsShadow = *o;
          h.sizeShadow = sz(fourCc("MCSH"));
        }
        if (auto o = ofs(fourCc("MCLQ")); o) {
          h.ofsLiquid = *o;
          h.sizeLiquid = sz(fourCc("MCLQ"));
        }
        if (auto o = ofs(fourCc("MCRF")); o) h.ofsRefs = *o;
        if (ofs(fourCc("MCRF")) || ofs(fourCc("MCRD")) || ofs(fourCc("MCRW"))) {
          h.nDoodadRefs = static_cast<std::uint32_t>(doodadRefs.size());
          h.nMapObjRefs = static_cast<std::uint32_t>(objectRefs.size());
        }
        if (auto o = ofs(fourCc("MCSE")); o) {
          h.ofsSndEmitters = *o;
          using Entry = std::remove_cvref_t<decltype(soundEmitters )>::value_type;
          h.nSndEmitters = static_cast<std::uint32_t>(mcseRaw.empty()
                                                          ? soundEmitters.size()
                                                          : mcseRaw.size() / sizeof(Entry));
        }
        std::memcpy(out.data() + base, &h, sizeof(SMChunk));
      }

      /** Compute the true length of a sub-chunk, correcting the unreliable
          declared sizes: MCAL/MCLQ are governed by the MCNK header (sizeAlpha /
          sizeLiquid, which include the 8-byte chunk header), and MCNR carries 13
          undeclared trailing bytes pre-Cata. MCSH's own size IS reliable, and on a
          headerless split file every sub-chunk trusts its own size.
          @param magic    the sub-chunk fourcc.
          @param declared the sub-chunk's own declared size.
          @param kind     the physical file (the header is only present with one).
          @return the corrected byte length. */
      std::uint32_t _subchunk_length(std::uint32_t magic, std::uint32_t declared, FileKind kind) const {
        if (magic == fourCc("MCNR") && declared <= 435) return 448;
        if (fileHasHeader(kind)) {
          // The header's size fields are authoritative for MCAL/MCLQ even when
          // they say "empty" (<= 8, header only): the vanilla map tool wrote
          // garbage declared sizes into empty sub-chunk headers (every
          // AhnQiraj MCAL with no alpha data declares -2048 — 243 tiles in
          // the 1.12.1 fleet audit).
          if (magic == fourCc("MCAL") && header.sizeAlpha != 0)
            return header.sizeAlpha <= 8
                     ? 0
                     : header.sizeAlpha - 8;
          if (magic == fourCc("MCLQ") && header.sizeLiquid != 0)
            return header.sizeLiquid <= 8
                     ? 0
                     : header.sizeLiquid - 8;
        }
        return declared;
      }

      /** Zero the DERIVED binary fields after a portion is read: each layer's MCAL
          offset (re-derived on write) and, on a header-bearing file, the MCNK
          header's offset/size/count fields — keeping them would make the semantic
          round-trip compare layout artifacts. ofsHeight/ofsNormal are the 64-bit
          hole mask when HighResHoles is set, so they are preserved then.
          @param kind the physical file just read. */
      void _clear_derived_on_read(FileKind kind) {
        for (auto& layer : layers) layer.offsetInMcal = 0;
        if (!fileHasHeader(kind)) return;
        if (!hasFlag(header.flags, MapChunkFlags::HighResHoles)) {
          header.ofsHeight = 0;
          header.ofsNormal = 0;
        }
        header.nLayers = 0;
        header.nDoodadRefs = 0;
        header.ofsLayer = header.ofsRefs = header.ofsAlpha = header.sizeAlpha = 0;
        header.ofsShadow = header.sizeShadow = 0;
        header.nMapObjRefs = 0;
        header.ofsSndEmitters = header.nSndEmitters = 0;
        header.ofsLiquid = header.sizeLiquid = 0;
        header.ofsMccv = header.ofsMclv = 0;
      }
    };
  }

  /** A terrain chunk — the canonicalizing face of detail::MapChunk: every client
      version collapses to its range's first grid version (MapChunkPivots). */
  template <ClientVersion V>
  using MapChunk = detail::MapChunk<canonicalVersion(V, MapChunkPivots, AdtVersions)>;

  namespace detail {
    template <ClientVersion V>
    Result<void> MapChunk<V>::read_from(std::span<const std::byte> payload, FileKind kind, AlphaFormat af) {
      using Self = MapChunk<V>;
      static constexpr auto Members = formats::detail::membersOf<Self>();

      std::size_t pos = 0;
      if (fileHasHeader(kind)) {
        if (payload.size() < sizeof(SMChunk))
          return makeError(ErrorCode::ChunkTruncated,
                            std::format("MCNK header needs {} bytes, got {}", sizeof(SMChunk), payload.size()));
        std::memcpy(&header, payload.data(), sizeof(SMChunk));
        pos = sizeof(SMChunk);
      }

      const MapChunkReadCtx ctx{
        .header = header,
        .layers = layers,
        .af = af,
        .fix = !hasFlag(header.flags, MapChunkFlags::DoNotFixAlphaMap),
        .kind = kind
      };

      while (pos + 8 <= payload.size()) {
        std::uint32_t magic = 0, declared = 0;
        std::memcpy(&magic, payload.data() + pos, 4);
        std::memcpy(&declared, payload.data() + pos + 4, 4);
        const std::size_t dataAt = pos + 8;

        std::uint32_t effective = _subchunk_length(magic, declared, kind);
        if (dataAt + effective > payload.size()) effective = declared; // last resort: trust the declared size
        if (dataAt + effective > payload.size())
          return makeError(ErrorCode::ChunkTruncated,
                            std::format("MCNK sub-chunk {} overruns the chunk", fourccToString(magic)));
        const auto sub = payload.subspan(dataAt, effective);
        pos = dataAt + effective;

        // MCRF is the one sub-chunk mapping to two members (doodad + object refs,
        // split by the header count) — the MCNK analogue of ADT's special MCNK.
        if (magic == fourCc("MCRF")) {
          _read_combined_refs(sub, kind);
          continue;
        }

        // Every other sub-chunk routes to its `chunk()`-annotated member by
        // fourcc: uniform members transfer through readValue, the ones that need
        // a codec (external length / transform / companion member) go through it.
        Result<void> outcome{};
        bool matched = false;
        template for (constexpr auto m : Members) {
          if constexpr (constexpr auto spec = formats::detail::annotation<formats::detail::ChunkSpec, m>(); spec.
            has_value()) {
            if (!matched && magic == spec->magic) {
              matched = true;
              if constexpr (constexpr auto ser = formats::detail::annotation<SerializerSpec, m>(); ser.has_value()) {
                using Codec = [:ser->codec:];
                outcome = Codec::read(*this, sub, ctx);
              }
              else
                outcome = formats::detail::readValue(this->[:m:], sub, magic, pos, spec->endian);
            }
          }
        }
        if (!outcome) return outcome;
      }

      _clear_derived_on_read(kind);
      return {};
    }

    template <ClientVersion V>
    Result<void> MapChunk<V>::write_to(FileBuffer& out, FileKind kind, AlphaFormat af) const {
      using Self = MapChunk<V>;
      static constexpr auto Members = formats::detail::membersOf<Self>();

      const std::size_t base = out.size();
      if (fileHasHeader(kind)) appendBytes(out, &header, sizeof(SMChunk));

      // Pre-build the alpha layout once: MCLY must emit each layer's derived
      // offsetInMcal, which MCAL computes, yet MCLY precedes MCAL on disk.
      std::vector<std::byte> alphaBlob;
      std::vector<SMLayer> stampedLayers;
      if (routesTo(InFile::Tex, kind)) AlphaCodec::prepare(*this, af, alphaBlob, stampedLayers);
      const MapChunkWriteCtx wctx{.stampedLayers = stampedLayers, .alphaBlob = alphaBlob};

      std::array<Emitted, SubChunkOrder.size() + 2> emitted{};
      // +2 for MCRD/MCRW
      std::size_t nEmitted = 0;

      for (const std::uint32_t want : SubChunkOrder) {
        template for (constexpr auto m : Members) {
          if constexpr (constexpr auto spec = formats::detail::annotation<formats::detail::ChunkSpec, m>(); spec.
            has_value()) {
            if constexpr (constexpr auto route = formats::detail::annotation<InFileSpec, m>(); route.has_value()) {
              if (spec->magic == want && routesTo(route->file, kind)) {
                if constexpr (constexpr auto ser = formats::detail::annotation<SerializerSpec, m>(); ser.has_value()) {
                  using Codec = [:ser->codec:];
                  if (Codec::engaged(*this, wctx)) {
                    const std::size_t before = out.size();
                    const std::uint32_t o = _emitSubchunk(out, base, want, [&] { Codec::write(*this, out, wctx); });
                    emitted[nEmitted++] = {want, o, static_cast<std::uint32_t>(out.size() - before)};
                  }
                }
                else {
                  // splice the member HERE (m is a constant expression); inside the
                  // lambda m is captured by reference and would not be.
                  const auto& member = this->[:m:];
                  if (!member.empty()) {
                    const std::size_t before = out.size();
                    const std::uint32_t o = _emitSubchunk(out, base, want, [&] {
                      std::ignore = formats::detail::writeValue(member, out);
                    });
                    emitted[nEmitted++] = {want, o, static_cast<std::uint32_t>(out.size() - before)};
                  }
                }
              }
            }
          }
        }
      }

      // References are the era-shifting special (MCRF vs MCRD/MCRW), emitted last.
      if (routesTo(InFile::Obj, kind)) _write_refs(out, base, kind, emitted, nEmitted);

      if (fileHasHeader(kind))
        _stamp_header(out, base, std::span<const Emitted>{emitted.data(), nEmitted});
      return {};
    }
  }
}
