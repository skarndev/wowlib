#pragma once

/** @file
    The per-cell terrain entity (namespace wowlib::formats::adt): MapChunk<V>,
    one of the 256 MCNK cells of a tile, fully decoded.

    MapChunk is NOT a generic ChunkedFile: the MCNK payload has a 128-byte header
    of derived offsets/sizes, sub-chunks whose lengths the header (not their own
    size field) governs (MCAL/MCLQ/MCSH), a pre-Cata MCNR trailer of 13 undeclared
    padding bytes, and alpha maps in three on-disk encodings — so it owns a bespoke
    reader/writer. Because a Cata+ tile splits one cell across the root, _tex0 and
    _obj0 files, the reader/writer work on one FILE's SLICE at a time
    (read_slice / write_slice), accumulating into the same MapChunk across files;
    a pre-Cata tile is one "monolithic" slice carrying every sub-chunk.

    Version-gated members live in conditionally-inherited trait bases (adt::detail):
    vertex colors since WotLK, vertex lighting / terrain materials since Cata, the
    legacy MCLQ liquid until WotLK. */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/adt/alpha_codec.hpp>
#include <wowlib/formats/adt/boundaries.hpp>
#include <wowlib/formats/adt/chunks/header.hpp>
#include <wowlib/formats/adt/chunks/liquid.hpp>
#include <wowlib/formats/adt/chunks/object.hpp>
#include <wowlib/formats/adt/chunks/texture.hpp>
#include <wowlib/formats/adt/liquid.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/common/version_range.hpp>
#include <wowlib/formats/common/version_slot.hpp>

namespace wowlib::formats::adt
{
  using namespace wowlib::formats::adt::chunks;

  /** Which physical file a chunk slice belongs to. Pre-Cata tiles are one
      `monolithic` file carrying every chunk; Cata+ tiles distribute chunks over
      the rest. */
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The physical ADT file a chunk belongs to. Pre-Cataclysm tiles are a single
        `monolithic` .adt; Cataclysm split the tile into a `root` .adt and the
        `tex0` / `obj0` / `obj1` / `lod` satellites. wowlib presents one unified
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

  /** The version-agnostic base of every MapChunk<V> (welded as "MapChunk"): the
      language bindings attach for_version here and give the per-version cells a
      common welded supertype. No role in the C++ API. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("MapChunk"),
    =welder::doc(R"(
        A terrain cell (MCNK), abstract over the client version. Usually obtained
        from ADT.cells rather than constructed; the per-version MapChunk* classes
        are subclasses. Construct a concrete version with
        MapChunk.for_version(expansion). See https://wowdev.wiki/ADT/v18#MCNK_chunk.)")
  ]] MapChunkBase
  {
  };

  /** Whether a slice of this kind carries the 128-byte MCNK header (root or the
      pre-Cata monolithic file). */
  constexpr bool slice_has_header(FileKind kind)
  {
    return kind == FileKind::monolithic || kind == FileKind::root;
  }

  /** Whether a chunk routed to the root file participates in this slice. */
  constexpr bool slice_is_root(FileKind kind)
  {
    return kind == FileKind::monolithic || kind == FileKind::root;
  }
  constexpr bool slice_is_tex(FileKind kind)
  {
    return kind == FileKind::monolithic || kind == FileKind::tex0;
  }
  constexpr bool slice_is_obj(FileKind kind)
  {
    return kind == FileKind::monolithic || kind == FileKind::obj0;
  }

  namespace detail
  {
    /** Vertex colors (MCCV), WotLK+. */
    struct MapChunkColor
    {
      [[=welder::doc("Per-vertex colors (MCCV, WotLK+): 145 BGRA entries blended onto the "
                     "terrain (0x7F = neutral)."),
        =welder::mark::no_reassign]]
      std::vector<CImVector> vertex_colors;

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkColor&) const = default;
    };

    /** Vertex lighting (MCLV) and terrain materials (MCMT), Cata+. */
    struct MapChunkCata
    {
      [[=welder::doc("Per-vertex baked lighting (MCLV, Cata+): 145 ARGB entries from "
                     "level-designer omni lights."),
        =welder::mark::no_reassign]]
      std::vector<CArgb> vertex_lighting;

      [[=welder::doc("Per-layer terrain material ids (MCMT, Cata+)."),
        =welder::mark::no_reassign]]
      std::vector<SMTerrainMaterial> material_ids;

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkCata&) const = default;
    };

    /** Legacy liquid (MCLQ), removed at WotLK (MH2O replaces it). */
    struct MapChunkLegacyLiquid
    {
      [[=welder::doc("The legacy per-cell liquid (MCLQ, pre-WotLK).")]]
      MCLQData legacy_liquid{};

      [[=welder::mark::exclude]]

      bool operator==(const MapChunkLegacyLiquid&) const = default;
    };
  }

  namespace detail
  {
    /** A terrain cell (MCNK) for one client version. Instantiate through the
        canonicalizing adt::MapChunk alias. */
    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc(R"(
          One terrain cell (MCNK) of a map tile, fully decoded: the cell header, the
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
        slot<V, ClientVersion{0, 0, 0, 0}, MapChunkLegacyLiquid, builds::WotLK>
    {
      static constexpr ClientVersion version = V;

      [[=welder::doc("The cell header (flags, grid position, area, holes, origin).")]]
      SMChunk header{};

      [[=welder::doc("The 9x9 + 8x8 = 145 terrain heights (MCVT), relative to the cell "
                     "origin, in the interleaved outer/inner row order."),
        =welder::mark::no_reassign]]
      std::vector<float> heights;

      [[=welder::doc("The 145 terrain normals (MCNR), X Z Y signed bytes."),
        =welder::mark::no_reassign]]
      std::vector<MCNREntry> normals;

      [[=welder::doc("The texture layers (MCLY); layer 0 is opaque, later layers blend "
                     "through their alpha map."),
        =welder::mark::no_reassign]]
      std::vector<SMLayer> layers;

      [[=welder::doc("One decoded 64x64 (4096-byte) alpha map per layer, aligned with "
                     "layers (layer 0's is empty); 0 = base texture, 255 = this layer."),
        =welder::mark::no_reassign]]
      std::vector<std::vector<std::uint8_t>> alpha_maps;

      [[=welder::doc("The decoded 64x64 (4096-byte) shadow map (MCSH), 0/1 per texel; "
                     "empty when the cell casts no baked shadow."),
        =welder::mark::no_reassign]]
      std::vector<std::uint8_t> shadow_map;

      [[=welder::doc("Doodad references (MCRF doodad part pre-Cata, MCRD Cata+): indices "
                     "into the tile's MDDF placements drawn in this cell."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> doodad_refs;

      [[=welder::doc("Object references (MCRF object part pre-Cata, MCRW Cata+): indices "
                     "into the tile's MODF placements drawn in this cell."),
        =welder::mark::no_reassign]]
      std::vector<std::uint32_t> object_refs;

      [[=welder::doc("Sound emitters placed in this cell (MCSE)."),
        =welder::mark::no_reassign]]
      std::vector<CWSoundEmitter> sound_emitters;

      /** The 13 undeclared trailing bytes after the MCNR normals (a near-constant
          client pattern, not derived from the normals); preserved for the
          semantic round-trip. */
      [[=welder::mark::exclude]]
      std::array<std::uint8_t, 13> mcnr_padding{};

      // --- slice serialization (definitions below) ---------------------------

      /** Decode one physical file's slice of this cell into the accumulating
          entity. Header-bearing slices parse the 128-byte header first.
          @param payload the MCNK chunk payload for this file.
          @param kind    which file the slice came from.
          @param af      the tile's alpha-map bit depth (from the WDT).
          @return a structural error or success. */
      [[=welder::mark::exclude]]
      Result<void> read_slice(std::span<const std::byte> payload, FileKind kind,
                              AlphaFormat af);

      /** Append this cell's slice for @a kind to @a out (the MCNK payload; the
          caller wraps the fourcc+size). Header-bearing slices emit the 128-byte
          header with every offset/size/count field freshly stamped.
          @param out  the destination buffer.
          @param kind which file's slice to write.
          @param af   the tile's alpha-map bit depth.
          @return a structural error or success. */
      [[=welder::mark::exclude]]
      Result<void> write_slice(FileBuffer& out, FileKind kind, AlphaFormat af) const;

      [[=welder::mark::exclude]]
      bool operator==(const MapChunk&) const = default;
    };
  }

  /** A terrain cell — the canonicalizing face of detail::MapChunk: every client
      version collapses to its range's first grid version (map_chunk_pivots). */
  template <ClientVersion V>
  using MapChunk = detail::MapChunk<canonical_version(V, map_chunk_pivots, adt_versions)>;

  namespace detail
  {
    inline constexpr std::size_t mcvt_count = 145;  // 9*9 + 8*8

    template <ClientVersion V>
    Result<void> MapChunk<V>::read_slice(std::span<const std::byte> payload, FileKind kind,
                                         AlphaFormat af)
    {
      std::size_t pos = 0;
      if (slice_has_header(kind))
      {
        if (payload.size() < sizeof(SMChunk))
          return make_error(ErrorCode::ChunkTruncated,
                            std::format("MCNK header needs {} bytes, got {}",
                                        sizeof(SMChunk), payload.size()));
        std::memcpy(&header, payload.data(), sizeof(SMChunk));
        pos = sizeof(SMChunk);
      }

      const bool do_not_fix =
        header.flags & static_cast<std::uint32_t>(MapChunkFlags::do_not_fix_alpha_map);

      while (pos + 8 <= payload.size())
      {
        std::uint32_t magic = 0;
        std::uint32_t declared = 0;
        std::memcpy(&magic, payload.data() + pos, 4);
        std::memcpy(&declared, payload.data() + pos + 4, 4);
        const std::size_t data_at = pos + 8;

        // Length corrections: the MCNK header, not the sub-chunk's own size,
        // governs MCAL/MCLQ/MCSH; MCNR carries 13 undeclared trailing bytes
        // pre-Cata. Fall back to the declared size when the header is absent
        // (split tex/obj slices) or its size field is zero.
        // Length corrections. MCAL's own size field is unreliable — the MCNK
        // header's size_alpha (which INCLUDES the 8-byte chunk header) governs;
        // MCLQ likewise (size_liquid, 8 == empty). MCNR carries 13 undeclared
        // trailing bytes pre-Cata. MCSH's own size IS reliable (== the header's
        // size_shadow, which unlike size_alpha does NOT count the chunk header),
        // so it needs no correction.
        std::size_t effective = declared;
        if (magic == four_cc("MCNR") && declared <= 435)
          effective = 448;
        else if (magic == four_cc("MCAL") && slice_has_header(kind) && header.size_alpha > 8)
          effective = header.size_alpha - 8;
        else if (magic == four_cc("MCLQ") && slice_has_header(kind) && header.size_liquid > 8)
          effective = header.size_liquid - 8;

        if (data_at + effective > payload.size())
          effective = declared;  // last resort: trust the declared size
        if (data_at + effective > payload.size())
          return make_error(ErrorCode::ChunkTruncated,
                            std::format("MCNK sub-chunk {} overruns the cell",
                                        fourcc_to_string(magic)));
        const auto sub = payload.subspan(data_at, effective);
        const auto to_u32 = [&](std::size_t at) {
          std::uint32_t v = 0;
          std::memcpy(&v, sub.data() + at, 4);
          return v;
        };

        if (magic == four_cc("MCVT"))
        {
          heights.resize(mcvt_count);
          std::memcpy(heights.data(), sub.data(), std::min(sub.size(), mcvt_count * 4));
        }
        else if (magic == four_cc("MCNR"))
        {
          normals.resize(mcvt_count);
          std::memcpy(normals.data(), sub.data(), std::min(sub.size(), mcvt_count * 3));
          if (sub.size() >= mcvt_count * 3 + 13)
            std::memcpy(mcnr_padding.data(), sub.data() + mcvt_count * 3, 13);
        }
        else if (magic == four_cc("MCLY"))
        {
          layers.resize(sub.size() / 16);
          std::memcpy(layers.data(), sub.data(), layers.size() * 16);
          // size the alpha maps from the layer count, NOT from MCAL presence:
          // MCAL is present in every pre-Cata MCNK but omitted when empty on
          // Cata+, and a per-layer alpha_maps entry keeps both consistent.
          alpha_maps.assign(layers.size(), {});
        }
        else if (magic == four_cc("MCAL"))
        {
          if (alpha_maps.size() < layers.size())
            alpha_maps.resize(layers.size());
          for (std::size_t i = 0; i < layers.size(); ++i)
          {
            const std::uint32_t f = layers[i].flags;
            if (!(f & static_cast<std::uint32_t>(LayerFlags::use_alpha_map)))
              continue;
            const std::size_t off = layers[i].offset_in_mcal;
            if (off > sub.size())
              continue;
            const auto layer_src = sub.subspan(off);
            if (f & static_cast<std::uint32_t>(LayerFlags::alpha_map_compressed))
              adt::detail::decode_alpha_rle(layer_src, alpha_maps[i]);
            else if (af == AlphaFormat::highres_8bit)
              adt::detail::decode_alpha_8bit(layer_src, alpha_maps[i]);
            else
              adt::detail::decode_alpha_4bit(layer_src, alpha_maps[i]);
            if (!do_not_fix)
              adt::detail::fix_last_row_col(alpha_maps[i]);
          }
        }
        else if (magic == four_cc("MCSH"))
        {
          adt::detail::decode_shadow(sub, shadow_map);
          if (!do_not_fix)
            adt::detail::fix_last_row_col(shadow_map);
        }
        else if (magic == four_cc("MCRF"))
        {
          const std::size_t n_dd = slice_has_header(kind) ? header.n_doodad_refs : 0;
          const std::size_t total = sub.size() / 4;
          const std::size_t dd = std::min(n_dd, total);
          doodad_refs.resize(dd);
          object_refs.resize(total - dd);
          for (std::size_t i = 0; i < dd; ++i)
            doodad_refs[i] = to_u32(i * 4);
          for (std::size_t i = 0; i < total - dd; ++i)
            object_refs[i] = to_u32((dd + i) * 4);
        }
        else if (magic == four_cc("MCRD"))
        {
          doodad_refs.resize(sub.size() / 4);
          std::memcpy(doodad_refs.data(), sub.data(), doodad_refs.size() * 4);
        }
        else if (magic == four_cc("MCRW"))
        {
          object_refs.resize(sub.size() / 4);
          std::memcpy(object_refs.data(), sub.data(), object_refs.size() * 4);
        }
        else if (magic == four_cc("MCSE"))
        {
          sound_emitters.resize(sub.size() / sizeof(CWSoundEmitter));
          std::memcpy(sound_emitters.data(), sub.data(),
                      sound_emitters.size() * sizeof(CWSoundEmitter));
        }
        else if (magic == four_cc("MCCV"))
        {
          if constexpr (requires { this->vertex_colors; })
          {
            this->vertex_colors.resize(mcvt_count);
            std::memcpy(this->vertex_colors.data(), sub.data(),
                        std::min(sub.size(), mcvt_count * sizeof(CImVector)));
          }
        }
        else if (magic == four_cc("MCLV"))
        {
          if constexpr (requires { this->vertex_lighting; })
          {
            this->vertex_lighting.resize(mcvt_count);
            std::memcpy(this->vertex_lighting.data(), sub.data(),
                        std::min(sub.size(), mcvt_count * sizeof(CArgb)));
          }
        }
        else if (magic == four_cc("MCMT"))
        {
          if constexpr (requires { this->material_ids; })
          {
            this->material_ids.resize(sub.size() / sizeof(SMTerrainMaterial));
            std::memcpy(this->material_ids.data(), sub.data(),
                        this->material_ids.size() * sizeof(SMTerrainMaterial));
          }
        }
        else if (magic == four_cc("MCLQ"))
        {
          if constexpr (requires { this->legacy_liquid; })
            if (auto r = this->legacy_liquid.read(sub); !r)
              return r;
        }
        // other sub-chunks (MCBB, MCDD, MPTX, …) are not modeled this stage and
        // are skipped; a later stage stores them for round-trip.
        pos = data_at + effective;
      }

      // Each layer's offset into the MCAL blob is derived (re-stamped on write
      // from the alpha layout); zero it once MCAL has been decoded so the
      // semantic diff does not compare a layout artifact.
      for (auto& layer : layers)
        layer.offset_in_mcal = 0;

      // Normalize the header's DERIVED fields (offsets, sizes, counts) to zero
      // once they have been consumed: they describe a specific on-disk layout,
      // and wowlib re-derives its own on write — keeping the read values would
      // make the semantic round-trip (parse == parse(write(parse))) compare
      // layout artifacts. The authored fields (flags, indices, area, holes,
      // origin, detail-doodad maps) stay. ofs_height/ofs_normal are the 64-bit
      // hole mask when high_res_holes is set, so they are preserved then.
      if (slice_has_header(kind))
      {
        if (!(header.flags
              & static_cast<std::uint32_t>(MapChunkFlags::high_res_holes)))
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
      return {};
    }

    template <ClientVersion V>
    Result<void> MapChunk<V>::write_slice(FileBuffer& out, FileKind kind, AlphaFormat af) const
    {
      const std::size_t base = out.size();
      const bool header_here = slice_has_header(kind);
      if (header_here)
      {
        const auto* hb = reinterpret_cast<const std::byte*>(&header);
        out.insert(out.end(), hb, hb + sizeof(SMChunk));
      }

      // emit one sub-chunk, returning its chunk-start-relative offset (8 + the
      // payload-local offset of its fourcc).
      std::uint32_t emitted_size_alpha = 0, emitted_size_shadow = 0, emitted_size_liquid = 0;
      const auto emit = [&](const char (&cc)[5], auto&& body) -> std::uint32_t {
        const std::uint32_t ofs = static_cast<std::uint32_t>(out.size() - base) + 8;
        std::uint32_t magic = four_cc(cc);
        const auto* mb = reinterpret_cast<const std::byte*>(&magic);
        out.insert(out.end(), mb, mb + 4);
        const std::size_t size_at = out.size();
        out.insert(out.end(), 4, std::byte{0});
        body();
        const auto size = static_cast<std::uint32_t>(out.size() - size_at - 4);
        std::memcpy(out.data() + size_at, &size, 4);
        return ofs;
      };
      const auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
      };

      // track which offsets to stamp; those not emitted keep the read header's
      // value (so a split root's tex/obj-owned fields round-trip untouched).
      std::optional<std::uint32_t> o_mcvt, o_mcnr, o_mccv, o_mclv, o_mcly, o_mcrf,
        o_mcal, o_mcsh, o_mcse, o_mclq;
      bool wrote_layers = false, wrote_refs = false, wrote_snd = false;

      if (slice_is_root(kind))
      {
        if (!heights.empty())
          o_mcvt = emit("MCVT", [&] { put(heights.data(), heights.size() * 4); });
        if constexpr (requires { this->vertex_colors; })
          if (!this->vertex_colors.empty())
            o_mccv = emit("MCCV", [&] {
              put(this->vertex_colors.data(), this->vertex_colors.size() * sizeof(CImVector));
            });
        if constexpr (requires { this->vertex_lighting; })
          if (!this->vertex_lighting.empty())
            o_mclv = emit("MCLV", [&] {
              put(this->vertex_lighting.data(), this->vertex_lighting.size() * sizeof(CArgb));
            });
        if (!normals.empty())
          o_mcnr = emit("MCNR", [&] {
            put(normals.data(), normals.size() * 3);
            put(mcnr_padding.data(), 13);
          });
        if constexpr (requires { this->legacy_liquid; })
          if (!this->legacy_liquid.empty())
          {
            const std::size_t before = out.size();
            o_mclq = emit("MCLQ", [&] { (void)this->legacy_liquid.write(out); });
            emitted_size_liquid = static_cast<std::uint32_t>(out.size() - before);
          }
        if (!sound_emitters.empty())
        {
          o_mcse = emit("MCSE", [&] {
            put(sound_emitters.data(), sound_emitters.size() * sizeof(CWSoundEmitter));
          });
          wrote_snd = true;
        }
      }
      if (slice_is_tex(kind))
      {
        // Build the alpha blob first (each layer's MCAL offset points into it),
        // then emit MCLY, the independent MCSH shadow map, and MCAL.
        std::vector<std::byte> alpha_blob;
        std::vector<SMLayer> stamped = layers;
        for (std::size_t i = 0; i < stamped.size(); ++i)
        {
          const bool has = i < alpha_maps.size() && !alpha_maps[i].empty()
                           && (stamped[i].flags
                               & static_cast<std::uint32_t>(LayerFlags::use_alpha_map));
          stamped[i].offset_in_mcal = static_cast<std::uint32_t>(alpha_blob.size());
          if (!has)
            continue;
          if (stamped[i].flags & static_cast<std::uint32_t>(LayerFlags::alpha_map_compressed))
            adt::detail::encode_alpha_rle(alpha_maps[i], alpha_blob);
          else if (af == AlphaFormat::highres_8bit)
            adt::detail::encode_alpha_8bit(alpha_maps[i], alpha_blob);
          else
            adt::detail::encode_alpha_4bit(alpha_maps[i], alpha_blob);
        }
        if (!layers.empty())
        {
          o_mcly = emit("MCLY", [&] { put(stamped.data(), stamped.size() * 16); });
          wrote_layers = true;
        }
        if (!shadow_map.empty())
        {
          const std::size_t before = out.size();
          o_mcsh = emit("MCSH", [&] {
            std::vector<std::byte> packed;
            adt::detail::encode_shadow(shadow_map, packed);
            put(packed.data(), packed.size());
          });
          emitted_size_shadow = static_cast<std::uint32_t>(out.size() - before);
        }
        // MCAL is omitted when the cell has no alpha data (most cells): emitting
        // an empty one would make a no-MCAL cell round-trip to a 1-entry
        // alpha_maps. Emit only when at least one layer contributed a map.
        if (!alpha_blob.empty())
        {
          const std::size_t before = out.size();
          o_mcal = emit("MCAL", [&] { put(alpha_blob.data(), alpha_blob.size()); });
          emitted_size_alpha = static_cast<std::uint32_t>(out.size() - before);
        }
        if constexpr (requires { this->material_ids; })
          if (!this->material_ids.empty())
            emit("MCMT", [&] {
              put(this->material_ids.data(), this->material_ids.size() * sizeof(SMTerrainMaterial));
            });
      }
      if (slice_is_obj(kind))
      {
        const bool split = kind != FileKind::monolithic;
        if (split)
        {
          if (!doodad_refs.empty())
            emit("MCRD", [&] { put(doodad_refs.data(), doodad_refs.size() * 4); });
          if (!object_refs.empty())
            emit("MCRW", [&] { put(object_refs.data(), object_refs.size() * 4); });
          wrote_refs = !doodad_refs.empty() || !object_refs.empty();
        }
        else if (!doodad_refs.empty() || !object_refs.empty())
        {
          o_mcrf = emit("MCRF", [&] {
            put(doodad_refs.data(), doodad_refs.size() * 4);
            put(object_refs.data(), object_refs.size() * 4);
          });
          wrote_refs = true;
        }
      }

      if (!header_here)
        return {};

      // stamp the header's derived fields for every sub-chunk emitted here; the
      // rest keep their read values (split slices preserve other files' fields).
      SMChunk h;
      std::memcpy(&h, out.data() + base, sizeof(SMChunk));
      const bool high_res =
        h.flags & static_cast<std::uint32_t>(MapChunkFlags::high_res_holes);
      if (o_mcvt && !high_res) h.ofs_height = *o_mcvt;
      if (o_mcnr && !high_res) h.ofs_normal = *o_mcnr;
      if (o_mccv) h.ofs_mccv = *o_mccv;
      if (o_mclv) h.ofs_mclv = *o_mclv;
      if (o_mcly) { h.ofs_layer = *o_mcly; h.n_layers = static_cast<std::uint32_t>(layers.size()); }
      if (o_mcal) { h.ofs_alpha = *o_mcal; h.size_alpha = emitted_size_alpha; }
      if (o_mcsh) { h.ofs_shadow = *o_mcsh; h.size_shadow = emitted_size_shadow; }
      if (o_mclq) { h.ofs_liquid = *o_mclq; h.size_liquid = emitted_size_liquid; }
      if (o_mcrf) h.ofs_refs = *o_mcrf;
      if (wrote_layers)
        h.n_layers = static_cast<std::uint32_t>(layers.size());
      if (wrote_refs)
      {
        h.n_doodad_refs = static_cast<std::uint32_t>(doodad_refs.size());
        h.n_map_obj_refs = static_cast<std::uint32_t>(object_refs.size());
      }
      if (wrote_snd)
      {
        h.ofs_snd_emitters = *o_mcse;
        h.n_snd_emitters = static_cast<std::uint32_t>(sound_emitters.size());
      }
      // The do_not_fix_alpha flag is NORMALIZED to set by the ADT reader after
      // every slice is in (adt.hpp), so the in-memory header already carries it;
      // write preserves it. We must not touch it here — the tex slice's alpha
      // decode still needs the ORIGINAL flag while other slices are read.
      std::memcpy(out.data() + base, &h, sizeof(SMChunk));
      return {};
    }
  }
}
