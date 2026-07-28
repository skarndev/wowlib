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

    Its sub-chunk members still carry the same `chunk()` + `in_file()` annotations
    the tile-level ADT chunks do, so read_from/write_to are REFLECTION-DRIVEN: a
    `template for` over the members matches the fourcc and routes by physical file.
    The UNIFORM sub-chunks (plain vectors / the SelfSerializing MCLQ) transfer
    through the chunk engine's read_value/write_value; the ones that break the
    member mapping (MCNR padding, MCLY, MCAL codec, MCSH codec) carry a
    `serialized_by<Codec>()` annotation naming a MapChunk-context codec. MCRF (one
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

namespace wowlib::formats::adt
{
  using namespace wowlib::formats::adt::chunks;

  /** Which physical ADT file a chunk's portion belongs to. Pre-Cata tiles are one
      `monolithic` file carrying every chunk; Cata+ tiles distribute chunks over
      the split ADT files (root/_tex0/_obj0/_obj1/_lod). */
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The physical ADT file a chunk belongs to. Pre-Cataclysm tiles are a single
        `monolithic` .adt; Cataclysm split the tile into a `root` .adt and the
        `tex0` / `obj0` / `obj1` / `lod` split files. wowlib presents one unified
        ADT regardless; this only surfaces in the low-level split-file API.)")
  ]] FileKind : std::uint8_t
  {
    monolithic [[=welder::doc("The single pre-Cataclysm .adt (every chunk).")]] = 0,
    root [[=welder::doc("The Cata+ root .adt (terrain heights/normals/colors, liquid, "
                        "sounds).")]] = 1,
    tex0 [[=welder::doc("The Cata+ _tex0.adt (texture layers, alpha/shadow maps, "
                        "materials).")]] = 2,
    obj0 [[=welder::doc("The Cata+ _obj0.adt (doodad/object references and "
                        "placements).")]] = 3,
    obj1 [[=welder::doc("The Cata+ _obj1.adt (level-of-detail object placements).")]] = 4,
    lod [[=welder::doc("The Legion+ _lod.adt (low-detail geometry and liquids).")]] = 5
  };

  /** The physical-file group a wire chunk (tile-level or MCNK sub-chunk) is routed
      to on write; the monolithic file carries every group. Used by both ADT tile
      chunks and MapChunk sub-chunks via the `in_file()` annotation. */
  enum class InFile : std::uint8_t
  {
    root,  /**< The root .adt (header, terrain, water, sounds, flying bounds). */
    tex,   /**< The _tex0.adt (textures, layers, alpha/shadow maps, materials). */
    obj    /**< The _obj0.adt (model/WMO names, placements, references). */
  };

  namespace detail
  {
    /** Stored form of an `in_file` annotation: a wire chunk's physical-file group. */
    struct in_file_spec
    {
      InFile file;
    };

    /** Stored form of a `serialized_by` annotation: the reflection of a codec type
        handling a sub-chunk that a plain read_value/write_value cannot (external
        length, a transform, or a companion member). */
    struct serializer_spec
    {
      std::meta::info codec;
    };
  }

  /** Annotate a wire chunk member with the physical file it is routed to on write
      (the monolithic file carries every group).
      @param file the physical-file group.
      @return the annotation payload. */
  consteval detail::in_file_spec in_file(InFile file) { return {file}; }

  /** Annotate a sub-chunk member with a MapChunk-context codec (a struct with
      static `read(self, span, ReadCtx)` / `write(self, out, WriteCtx)` /
      `engaged(self, WriteCtx)` templates) instead of the default uniform transfer.
      @param codec the reflection of the codec type, e.g. `^^AlphaCodec`.
      @return the annotation payload. */
  consteval detail::serializer_spec serialized_by(std::meta::info codec) { return {codec}; }

  /** Whether a member routed to @a file participates in physical file @a kind: the
      monolithic file carries every group, each split file only its own.
      @param file the member's routing group.
      @param kind the physical file being read/written.
      @return whether the member participates. */
  constexpr bool routes_to(InFile file, FileKind kind)
  {
    switch (kind)
    {
    case FileKind::monolithic: return true;
    case FileKind::root: return file == InFile::root;
    case FileKind::tex0: return file == InFile::tex;
    case FileKind::obj0: return file == InFile::obj;
    default: return false;
    }
  }

  /** Whether a given physical file carries the 128-byte MCNK header (the root or
      the pre-Cata monolithic file).
      @param kind the physical file.
      @return true for the monolithic and root files. */
  constexpr bool file_has_header(FileKind kind)
  {
    return kind == FileKind::monolithic || kind == FileKind::root;
  }

  /** The version-agnostic base of every MapChunk<V> (welded as "MapChunk"): the
      language bindings attach for_version here and give the per-version chunks a
      common welded supertype. No role in the C++ API. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("MapChunk"),
    =welder::doc(R"(
        A terrain chunk (MCNK), abstract over the client version. Usually obtained
        from ADT.chunks rather than constructed; the per-version MapChunk* classes
        are subclasses. Construct a concrete version with
        MapChunk.for_version(expansion). See https://wowdev.wiki/ADT/v18#MCNK_chunk.)")
  ]] MapChunkBase
  {
  };

  namespace detail
  {
    /** The 9x9 + 8x8 = 145 vertices of the MCVT height / MCNR normal grid. */
    inline constexpr std::size_t mcvt_count = 145;

    /** Context a sub-chunk codec needs to read one MCNK sub-chunk: the (already
        parsed) header, the sibling texture layers (MCAL indexes them), the tile
        alpha bit depth and whether to repair the 63x63 "unfixed" edge form. */
    struct MapChunkReadCtx
    {
      const SMChunk& header;
      const std::vector<SMLayer>& layers;
      AlphaFormat af;
      bool fix;
      FileKind kind;
    };

    /** Context a sub-chunk codec needs to write one MCNK sub-chunk. The alpha
        layout is pre-built once (MCLY must emit each layer's offset_in_mcal, which
        MCAL computes) into these two fields; the layer/alpha codecs read them. */
    struct MapChunkWriteCtx
    {
      const std::vector<SMLayer>& stamped_layers;  // layers with offset_in_mcal set
      std::span<const std::byte> alpha_blob;       // the pre-encoded MCAL blob
    };

    /** Append @a n raw bytes at @a p to @a out (a byte-copy primitive). */
    inline void append_bytes(FileBuffer& out, const void* p, std::size_t n)
    {
      const auto* b = static_cast<const std::byte*>(p);
      out.insert(out.end(), b, b + n);
    }

    /** MCNR codec: 145 XZY normal triples followed by 13 undeclared padding bytes
        (a near-constant client pattern preserved for the semantic round-trip). */
    struct NormalCodec
    {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub,
                               const MapChunkReadCtx&)
      {
        self.normals.resize(mcvt_count);
        std::memcpy(self.normals.data(), sub.data(), std::min(sub.size(), mcvt_count * 3));
        if (sub.size() >= mcvt_count * 3 + 13)
          std::memcpy(self.mcnr_padding.data(), sub.data() + mcvt_count * 3, 13);
        return {};
      }
      template <typename Chunk>
      static void write(const Chunk& self, FileBuffer& out, const MapChunkWriteCtx&)
      {
        append_bytes(out, self.normals.data(), self.normals.size() * 3);
        append_bytes(out, self.mcnr_padding.data(), 13);
      }
      template <typename Chunk>
      static bool engaged(const Chunk& self, const MapChunkWriteCtx&)
      {
        return !self.normals.empty();
      }
    };

    /** MCLY codec: the texture layers, plus sizing alpha_maps to the layer count
        (MCAL may be absent on Cata+ when empty, so alpha_maps must be sized here
        to keep it aligned with layers). */
    struct LayerCodec
    {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub,
                               const MapChunkReadCtx&)
      {
        self.layers.resize(sub.size() / 16);
        std::memcpy(self.layers.data(), sub.data(), self.layers.size() * 16);
        self.alpha_maps.assign(self.layers.size(), {});
        return {};
      }
      template <typename Chunk>
      static void write(const Chunk&, FileBuffer& out, const MapChunkWriteCtx& ctx)
      {
        append_bytes(out, ctx.stamped_layers.data(), ctx.stamped_layers.size() * 16);
      }
      template <typename Chunk>
      static bool engaged(const Chunk& self, const MapChunkWriteCtx&)
      {
        return !self.layers.empty();
      }
    };

    /** MCAL codec: decode/encode the per-layer 64x64 alpha maps through
        AlphaMapCodec, indexing the sibling layers' flags + offset_in_mcal. */
    struct AlphaCodec
    {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub,
                               const MapChunkReadCtx& ctx)
      {
        if (self.alpha_maps.size() != self.layers.size())
          self.alpha_maps.resize(self.layers.size());
        const AlphaMapCodec codec{ctx.af};
        for (auto&& [layer, out_map] : std::views::zip(self.layers, self.alpha_maps))
        {
          if (!has_flag(layer.flags, LayerFlags::use_alpha_map))
            continue;
          if (layer.offset_in_mcal > sub.size())
            continue;
          const bool compressed = has_flag(layer.flags, LayerFlags::alpha_map_compressed);
          out_map = codec.decode(sub.subspan(layer.offset_in_mcal), compressed, ctx.fix);
        }
        return {};
      }

      /** Build the MCAL blob and the layers copy carrying each layer's derived
          offset_in_mcal (MCLY emits that copy). Called once before the write walk.
          @param self    the chunk.
          @param af      the tile alpha bit depth.
          @param blob    receives the encoded MCAL bytes.
          @param stamped receives the layers with offset_in_mcal stamped. */
      template <typename Chunk>
      static void prepare(const Chunk& self, AlphaFormat af, std::vector<std::byte>& blob,
                          std::vector<SMLayer>& stamped)
      {
        stamped = self.layers;
        const AlphaMapCodec codec{af};
        for (std::size_t i = 0; i < stamped.size(); ++i)
        {
          const bool has = i < self.alpha_maps.size() && !self.alpha_maps[i].empty()
                           && has_flag(stamped[i].flags, LayerFlags::use_alpha_map);
          stamped[i].offset_in_mcal = static_cast<std::uint32_t>(blob.size());
          if (!has)
            continue;
          codec.encode(self.alpha_maps[i],
                       has_flag(stamped[i].flags, LayerFlags::alpha_map_compressed), blob);
        }
      }

      template <typename Chunk>
      static void write(const Chunk&, FileBuffer& out, const MapChunkWriteCtx& ctx)
      {
        append_bytes(out, ctx.alpha_blob.data(), ctx.alpha_blob.size());
      }
      template <typename Chunk>
      static bool engaged(const Chunk&, const MapChunkWriteCtx& ctx)
      {
        // MCAL is omitted when no layer carries a map (most chunks): emitting an
        // empty one would round-trip a no-MCAL chunk to a 1-entry alpha_maps.
        return !ctx.alpha_blob.empty();
      }
    };

    /** MCSH codec: the 64x64 shadow map through ShadowMapCodec. */
    struct ShadowCodec
    {
      template <typename Chunk>
      static Result<void> read(Chunk& self, std::span<const std::byte> sub,
                               const MapChunkReadCtx& ctx)
      {
        self.shadow_map = ShadowMapCodec{}.decode(sub, ctx.fix);
        return {};
      }
      template <typename Chunk>
      static void write(const Chunk& self, FileBuffer& out, const MapChunkWriteCtx&)
      {
        std::vector<std::byte> packed;
        ShadowMapCodec{}.encode(self.shadow_map, packed);
        append_bytes(out, packed.data(), packed.size());
      }
      template <typename Chunk>
      static bool engaged(const Chunk& self, const MapChunkWriteCtx&)
      {
        return !self.shadow_map.empty();
      }
    };

    /** Vertex colors (MCCV), WotLK+. */
    struct MapChunkColor
    {
      [[=chunk("MCCV"),
        =in_file(InFile::root),
        =welder::doc("Per-vertex colors (MCCV, WotLK+): 145 BGRA entries blended onto the "
                     "terrain (0x7F = neutral)."),
        =welder::mark::no_reassign]]
      std::vector<CImVector> vertex_colors;

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkColor&) const = default;
    };

    /** Vertex lighting (MCLV) and terrain materials (MCMT), Cata+. */
    struct MapChunkCata
    {
      [[=chunk("MCLV"),
        =in_file(InFile::root),
        =welder::doc("Per-vertex baked lighting (MCLV, Cata+): 145 ARGB entries from "
                     "level-designer omni lights."),
        =welder::mark::no_reassign]]
      std::vector<CArgb> vertex_lighting;

      [[=chunk("MCMT"),
        =in_file(InFile::tex),
        =welder::doc("Per-layer terrain material ids (MCMT, Cata+)."),
        =welder::mark::no_reassign]]
      std::vector<SMTerrainMaterial> material_ids;

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkCata&) const = default;
    };

    /** Legacy liquid (MCLQ). Available through WotLK and removed at Cata (MH2O
        supersedes it, but Outland tiles keep shipping MCLQ in WotLK clients). */
    struct MapChunkLegacyLiquid
    {
      [[=chunk("MCLQ"),
        =in_file(InFile::root),
        =welder::doc("The legacy per-chunk liquid (MCLQ, up to and including WotLK).")]]
      MCLQData legacy_liquid{};

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkLegacyLiquid&) const = default;
    };
  }

  namespace detail
  {
    /** A terrain chunk (MCNK) for one client version. Instantiate through the
        canonicalizing adt::MapChunk alias. */
    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
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
        slot<V, builds::WotLK, MapChunkColor>,
        slot<V, builds::Cata, MapChunkCata>,
        slot<V, ClientVersion{0, 0, 0, 0}, MapChunkLegacyLiquid, builds::Cata>
    {
      static constexpr ClientVersion version = V;

      /** The canonical sub-chunk write order (excluding the references, which are
          the MCRF-vs-MCRD/MCRW special case, emitted last). Each physical file
          emits the subset routed to it, in this order — see write_to. */
      static constexpr std::array sub_chunk_order{
        four_cc("MCVT"), four_cc("MCCV"), four_cc("MCLV"), four_cc("MCNR"),
        four_cc("MCLQ"), four_cc("MCSE"), four_cc("MCLY"), four_cc("MCSH"),
        four_cc("MCAL"), four_cc("MCMT")};

      [[=welder::doc("The chunk header (flags, grid position, area, holes, origin).")]]
      SMChunk header{};

      [[=chunk("MCVT"),
        =in_file(InFile::root),
        =welder::doc("The 9x9 + 8x8 = 145 terrain heights (MCVT), relative to the chunk "
                     "origin, in the interleaved outer/inner row order."),
        =welder::mark::no_reassign]]
      std::vector<float> heights;

      [[=chunk("MCNR"),
        =in_file(InFile::root),
        =serialized_by(^^detail::NormalCodec),
        =welder::doc("The 145 terrain normals (MCNR), X Z Y signed bytes."),
        =welder::mark::no_reassign]]
      std::vector<MCNREntry> normals;

      [[=chunk("MCLY"),
        =in_file(InFile::tex),
        =serialized_by(^^detail::LayerCodec),
        =welder::doc("The texture layers (MCLY); layer 0 is opaque, later layers blend "
                     "through their alpha map."),
        =welder::mark::no_reassign]]
      std::vector<SMLayer> layers;

      [[=chunk("MCAL"),
        =in_file(InFile::tex),
        =serialized_by(^^detail::AlphaCodec),
        =welder::doc("One decoded 64x64 (4096-byte) alpha map per layer, aligned with "
                     "layers (layer 0's is empty); 0 = base texture, 255 = this layer."),
        =welder::mark::no_reassign]]
      std::vector<std::vector<std::uint8_t>> alpha_maps;

      [[=chunk("MCSH"),
        =in_file(InFile::tex),
        =serialized_by(^^detail::ShadowCodec),
        =welder::doc("The decoded 64x64 (4096-byte) shadow map (MCSH), 0/1 per texel; "
                     "empty when the chunk casts no baked shadow."),
        =welder::mark::no_reassign]]
      std::vector<std::uint8_t> shadow_map;

      [[=chunk("MCRD"),
        =in_file(InFile::obj),
        =welder::doc("Doodad references (MCRF doodad part pre-Cata, MCRD Cata+): indices "
                     "into the tile's MDDF placements drawn in this chunk."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> doodad_refs;

      [[=chunk("MCRW"),
        =in_file(InFile::obj),
        =welder::doc("Object references (MCRF object part pre-Cata, MCRW Cata+): indices "
                     "into the tile's MODF placements drawn in this chunk."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> object_refs;

      [[=chunk("MCSE"),
        =in_file(InFile::root),
        =welder::doc("Sound emitters placed in this chunk (MCSE)."),
        =welder::mark::no_reassign]]
      std::vector<CWSoundEmitter> sound_emitters;

      /** The 13 undeclared trailing bytes after the MCNR normals (a near-constant
          client pattern, not derived from the normals); preserved for the
          semantic round-trip. Read/written as part of MCNR by NormalCodec. */
      [[=welder::mark::exclude]]
      std::array<std::uint8_t, 13> mcnr_padding{};

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
      struct Emitted
      {
        std::uint32_t magic = 0;
        std::uint32_t offset = 0;  // chunk-start-relative (points at the fourcc)
        std::uint32_t total = 0;   // fourcc + size + payload, for the size_* fields
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
      static std::uint32_t _emit_subchunk(FileBuffer& out, std::size_t base,
                                          std::uint32_t magic, Body&& body)
      {
        const auto ofs = static_cast<std::uint32_t>(out.size() - base) + 8;
        append_bytes(out, &magic, 4);
        const std::size_t size_at = out.size();
        out.insert(out.end(), 4, std::byte{0});
        body();
        const auto size = static_cast<std::uint32_t>(out.size() - size_at - 4);
        std::memcpy(out.data() + size_at, &size, 4);
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
      void _write_refs(FileBuffer& out, std::size_t base, FileKind kind,
                       std::span<Emitted> emitted, std::size_t& n) const
      {
        const auto emit = [&](std::uint32_t magic, auto&& body) {
          const std::size_t before = out.size();
          const std::uint32_t ofs = _emit_subchunk(out, base, magic, body);
          emitted[n++] = {magic, ofs, static_cast<std::uint32_t>(out.size() - before)};
        };
        if (kind != FileKind::monolithic)
        {
          if (!doodad_refs.empty())
            emit(four_cc("MCRD"), [&] { append_bytes(out, doodad_refs.data(), doodad_refs.size() * 4); });
          if (!object_refs.empty())
            emit(four_cc("MCRW"), [&] { append_bytes(out, object_refs.data(), object_refs.size() * 4); });
        }
        else if (!doodad_refs.empty() || !object_refs.empty())
        {
          emit(four_cc("MCRF"), [&] {
            append_bytes(out, doodad_refs.data(), doodad_refs.size() * 4);
            append_bytes(out, object_refs.data(), object_refs.size() * 4);
          });
        }
      }

      /** Split the combined pre-Cata MCRF list into doodad and object refs (the
          header's n_doodad_refs marks the boundary).
          @param sub  the MCRF data (u32 indices).
          @param kind the physical file (the header count is only trusted with one). */
      void _read_combined_refs(std::span<const std::byte> sub, FileKind kind)
      {
        const std::size_t total = sub.size() / 4;
        const std::size_t n_dd =
          std::min<std::size_t>(file_has_header(kind) ? header.n_doodad_refs : 0, total);
        doodad_refs.resize(n_dd);
        object_refs.resize(total - n_dd);
        std::memcpy(doodad_refs.data(), sub.data(), n_dd * 4);
        std::memcpy(object_refs.data(), sub.data() + n_dd * 4, (total - n_dd) * 4);
      }

      /** Stamp the derived MCNK header fields from the sub-chunks emitted here;
          fields not emitted keep their read value (so a split root preserves the
          tex/obj-owned fields). The do_not_fix_alpha flag is left as-is: the ADT
          reader normalizes it after every portion is in, so it is already correct.
          @param out     the destination buffer (the header sits at @a base).
          @param base    the MCNK payload start in @a out.
          @param emitted the sub-chunks emitted into this file. */
      void _stamp_header(FileBuffer& out, std::size_t base, std::span<const Emitted> emitted) const
      {
        SMChunk h;
        std::memcpy(&h, out.data() + base, sizeof(SMChunk));
        const bool high_res = has_flag(h.flags, MapChunkFlags::high_res_holes);
        const auto ofs = [&](std::uint32_t magic) -> std::optional<std::uint32_t> {
          for (const Emitted& e : emitted)
            if (e.magic == magic)
              return e.offset;
          return std::nullopt;
        };
        const auto sz = [&](std::uint32_t magic) -> std::uint32_t {
          for (const Emitted& e : emitted)
            if (e.magic == magic)
              return e.total;
          return 0;
        };

        if (auto o = ofs(four_cc("MCVT")); o && !high_res) h.ofs_height = *o;
        if (auto o = ofs(four_cc("MCNR")); o && !high_res) h.ofs_normal = *o;
        if (auto o = ofs(four_cc("MCCV")); o) h.ofs_mccv = *o;
        if (auto o = ofs(four_cc("MCLV")); o) h.ofs_mclv = *o;
        if (auto o = ofs(four_cc("MCLY")); o)
        {
          h.ofs_layer = *o;
          h.n_layers = static_cast<std::uint32_t>(layers.size());
        }
        if (auto o = ofs(four_cc("MCAL")); o) { h.ofs_alpha = *o; h.size_alpha = sz(four_cc("MCAL")); }
        if (auto o = ofs(four_cc("MCSH")); o) { h.ofs_shadow = *o; h.size_shadow = sz(four_cc("MCSH")); }
        if (auto o = ofs(four_cc("MCLQ")); o) { h.ofs_liquid = *o; h.size_liquid = sz(four_cc("MCLQ")); }
        if (auto o = ofs(four_cc("MCRF")); o) h.ofs_refs = *o;
        if (ofs(four_cc("MCRF")) || ofs(four_cc("MCRD")) || ofs(four_cc("MCRW")))
        {
          h.n_doodad_refs = static_cast<std::uint32_t>(doodad_refs.size());
          h.n_map_obj_refs = static_cast<std::uint32_t>(object_refs.size());
        }
        if (auto o = ofs(four_cc("MCSE")); o)
        {
          h.ofs_snd_emitters = *o;
          h.n_snd_emitters = static_cast<std::uint32_t>(sound_emitters.size());
        }
        std::memcpy(out.data() + base, &h, sizeof(SMChunk));
      }

      /** Compute the true length of a sub-chunk, correcting the unreliable
          declared sizes: MCAL/MCLQ are governed by the MCNK header (size_alpha /
          size_liquid, which include the 8-byte chunk header), and MCNR carries 13
          undeclared trailing bytes pre-Cata. MCSH's own size IS reliable, and on a
          headerless split file every sub-chunk trusts its own size.
          @param magic    the sub-chunk fourcc.
          @param declared the sub-chunk's own declared size.
          @param kind     the physical file (the header is only present with one).
          @return the corrected byte length. */
      std::uint32_t _subchunk_length(std::uint32_t magic, std::uint32_t declared,
                                     FileKind kind) const
      {
        if (magic == four_cc("MCNR") && declared <= 435)
          return 448;
        if (file_has_header(kind))
        {
          if (magic == four_cc("MCAL") && header.size_alpha > 8)
            return header.size_alpha - 8;
          if (magic == four_cc("MCLQ") && header.size_liquid > 8)
            return header.size_liquid - 8;
        }
        return declared;
      }

      /** Zero the DERIVED wire fields after a portion is read: each layer's MCAL
          offset (re-derived on write) and, on a header-bearing file, the MCNK
          header's offset/size/count fields — keeping them would make the semantic
          round-trip compare layout artifacts. ofs_height/ofs_normal are the 64-bit
          hole mask when high_res_holes is set, so they are preserved then.
          @param kind the physical file just read. */
      void _clear_derived_on_read(FileKind kind)
      {
        for (auto& layer : layers)
          layer.offset_in_mcal = 0;
        if (!file_has_header(kind))
          return;
        if (!has_flag(header.flags, MapChunkFlags::high_res_holes))
        {
          header.ofs_height = 0;
          header.ofs_normal = 0;
        }
        header.n_layers = 0;
        header.n_doodad_refs = 0;
        header.ofs_layer = header.ofs_refs = header.ofs_alpha = header.size_alpha = 0;
        header.ofs_shadow = header.size_shadow = 0;
        header.n_map_obj_refs = 0;
        header.ofs_snd_emitters = header.n_snd_emitters = 0;
        header.ofs_liquid = header.size_liquid = 0;
        header.ofs_mccv = header.ofs_mclv = 0;
      }
    };
  }

  /** A terrain chunk — the canonicalizing face of detail::MapChunk: every client
      version collapses to its range's first grid version (map_chunk_pivots). */
  template <ClientVersion V>
  using MapChunk = detail::MapChunk<canonical_version(V, map_chunk_pivots, adt_versions)>;

  namespace detail
  {
    template <ClientVersion V>
    Result<void> MapChunk<V>::read_from(std::span<const std::byte> payload, FileKind kind,
                                        AlphaFormat af)
    {
      using Self = MapChunk<V>;
      static constexpr auto members = formats::detail::members_of<Self>();

      std::size_t pos = 0;
      if (file_has_header(kind))
      {
        if (payload.size() < sizeof(SMChunk))
          return make_error(ErrorCode::ChunkTruncated,
                            std::format("MCNK header needs {} bytes, got {}",
                                        sizeof(SMChunk), payload.size()));
        std::memcpy(&header, payload.data(), sizeof(SMChunk));
        pos = sizeof(SMChunk);
      }

      const MapChunkReadCtx ctx{
        .header = header,
        .layers = layers,
        .af = af,
        .fix = !has_flag(header.flags, MapChunkFlags::do_not_fix_alpha_map),
        .kind = kind};

      while (pos + 8 <= payload.size())
      {
        std::uint32_t magic = 0, declared = 0;
        std::memcpy(&magic, payload.data() + pos, 4);
        std::memcpy(&declared, payload.data() + pos + 4, 4);
        const std::size_t data_at = pos + 8;

        std::uint32_t effective = _subchunk_length(magic, declared, kind);
        if (data_at + effective > payload.size())
          effective = declared;  // last resort: trust the declared size
        if (data_at + effective > payload.size())
          return make_error(ErrorCode::ChunkTruncated,
                            std::format("MCNK sub-chunk {} overruns the chunk",
                                        fourcc_to_string(magic)));
        const auto sub = payload.subspan(data_at, effective);
        pos = data_at + effective;

        // MCRF is the one sub-chunk mapping to two members (doodad + object refs,
        // split by the header count) — the MCNK analogue of ADT's special MCNK.
        if (magic == four_cc("MCRF"))
        {
          _read_combined_refs(sub, kind);
          continue;
        }

        // Every other sub-chunk routes to its `chunk()`-annotated member by
        // fourcc: uniform members transfer through read_value, the ones that need
        // a codec (external length / transform / companion member) go through it.
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
              if constexpr (constexpr auto ser =
                              formats::detail::annotation<serializer_spec, m>();
                            ser.has_value())
              {
                using Codec = [:ser->codec:];
                outcome = Codec::read(*this, sub, ctx);
              }
              else
                outcome = formats::detail::read_value(this->[:m:], sub, magic, pos, spec->endian);
            }
          }
        }
        if (!outcome)
          return outcome;
      }

      _clear_derived_on_read(kind);
      return {};
    }

    template <ClientVersion V>
    Result<void> MapChunk<V>::write_to(FileBuffer& out, FileKind kind, AlphaFormat af) const
    {
      using Self = MapChunk<V>;
      static constexpr auto members = formats::detail::members_of<Self>();

      const std::size_t base = out.size();
      if (file_has_header(kind))
        append_bytes(out, &header, sizeof(SMChunk));

      // Pre-build the alpha layout once: MCLY must emit each layer's derived
      // offset_in_mcal, which MCAL computes, yet MCLY precedes MCAL on disk.
      std::vector<std::byte> alpha_blob;
      std::vector<SMLayer> stamped_layers;
      if (routes_to(InFile::tex, kind))
        AlphaCodec::prepare(*this, af, alpha_blob, stamped_layers);
      const MapChunkWriteCtx wctx{.stamped_layers = stamped_layers, .alpha_blob = alpha_blob};

      std::array<Emitted, sub_chunk_order.size() + 2> emitted{};  // +2 for MCRD/MCRW
      std::size_t n_emitted = 0;

      for (const std::uint32_t want : sub_chunk_order)
      {
        template for (constexpr auto m : members)
        {
          if constexpr (constexpr auto spec =
                          formats::detail::annotation<formats::detail::chunk_spec, m>();
                        spec.has_value())
          {
            if constexpr (constexpr auto route = formats::detail::annotation<in_file_spec, m>();
                          route.has_value())
            {
              if (spec->magic == want && routes_to(route->file, kind))
              {
                if constexpr (constexpr auto ser =
                                formats::detail::annotation<serializer_spec, m>();
                              ser.has_value())
                {
                  using Codec = [:ser->codec:];
                  if (Codec::engaged(*this, wctx))
                  {
                    const std::size_t before = out.size();
                    const std::uint32_t o = _emit_subchunk(out, base, want,
                                                           [&] { Codec::write(*this, out, wctx); });
                    emitted[n_emitted++] = {want, o, static_cast<std::uint32_t>(out.size() - before)};
                  }
                }
                else
                {
                  // splice the member HERE (m is a constant expression); inside the
                  // lambda m is captured by reference and would not be.
                  const auto& member = this->[:m:];
                  if (!member.empty())
                  {
                    const std::size_t before = out.size();
                    const std::uint32_t o = _emit_subchunk(out, base, want, [&] {
                      std::ignore = formats::detail::write_value(member, out);
                    });
                    emitted[n_emitted++] = {want, o, static_cast<std::uint32_t>(out.size() - before)};
                  }
                }
              }
            }
          }
        }
      }

      // References are the era-shifting special (MCRF vs MCRD/MCRW), emitted last.
      if (routes_to(InFile::obj, kind))
        _write_refs(out, base, kind, emitted, n_emitted);

      if (file_has_header(kind))
        _stamp_header(out, base, std::span<const Emitted>{emitted.data(), n_emitted});
      return {};
    }
  }
}
