#pragma once

/** @file
    Structured ADT liquid entities (namespace wowlib::formats::adt): the modern
    MH2O (WotLK+) and the legacy MCLQ (up to and including WotLK), decoded to an
    editable form. The two coexist in WotLK — Northrend tiles use MH2O while
    Outland tiles still ship MCLQ.

    Both are intra-chunk OFFSET structures — a header whose fields address
    trailing variable-length data — so, like WMO's MLIQData, they own their
    payload encoding (SelfSerializing) rather than memcpy'ing. wowlib stores the
    fully-decoded liquid (heights, depths, texture coordinates, exists masks,
    attributes) and re-derives every binary offset on write; the guarantee is a
    semantic round-trip, not byte-identical bytes.

    MH2OData is version-independent: the WotLK+ MH2O layout is stable, and the
    per-instance vertex format is a runtime axis (resolved from the data, not the
    client version), so it is not templated on ClientVersion. */

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/adt/chunks/liquid.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::adt {
  using namespace wowlib::formats::adt::chunks;

  /** One liquid layer of one terrain cell, fully decoded (an MH2O instance).
      The vertex arrays are present per the vertex_format; the exists mask (when
      non-empty) selects which of the width x height tiles render. All binary
      offsets are derived on write. */
  struct [[
      =welder::weld,
      =welder::doc(R"(
        One liquid layer over a terrain cell (an MH2O instance), decoded. The
        heightmap/depthmap/uvmap arrays are present according to vertex_format and
        each hold (width + 1) x (height + 1) entries; exists_bitmap (when not
        empty) is a width x height bit grid selecting which tiles render. Heights
        outside [min_height, max_height] and the binary offsets are derived, not
        stored.)")
    ]] LiquidInstance {
    [[=welder::doc(
      "The liquid type (a LiquidType foreign key: 1 ocean, 2 ocean-flat, "
      "3 slime, 5/6 magma, …).")]]
    std::uint16_t liquid_type = 0;

    [[=welder::doc(
      "The stored liquid_object_or_lvf field: a LiquidObject id (>= 42) or "
      "a raw LiquidVertexFormat; vertex_format holds the resolved layout.")]]
    std::uint16_t liquid_object_or_lvf = 0;

    [[=welder::doc(
      "The resolved vertex layout wowlib decoded (and will re-lay).")]]
    LiquidVertexFormat vertex_format = LiquidVertexFormat::height_depth;

    [[=welder::doc(
      "Minimum surface height (the flat height when no heightmap).")]]
    float min_height = 0;

    [[=welder::doc("Maximum surface height.")]]
    float max_height = 0;

    [[=welder::doc("The liquid rectangle's x offset within the cell (0-7).")]]
    std::uint8_t x_offset = 0;
    [[=welder::doc("The liquid rectangle's y offset within the cell (0-7).")]]
    std::uint8_t y_offset = 0;
    [[=welder::doc("The liquid rectangle width in tiles (1-8).")]]
    std::uint8_t width = 8;
    [[=welder::doc("The liquid rectangle height in tiles (1-8).")]]
    std::uint8_t height = 8;

    [[=welder::doc(
        "The per-tile exists mask, width x height bits row-major (LSB first); "
        "empty means every tile renders."),
      =welder::mark::no_reassign]]
    std::vector<std::uint8_t> exists_bitmap;

    [[=welder::doc(
        "Surface heights, (width+1) x (height+1) row-major; present for "
        "vertex formats height_depth, height_uv, height_uv_depth."),
      =welder::mark::no_reassign]]
    std::vector<float> heightmap;

    [[=welder::doc("Depth bytes, (width+1) x (height+1) row-major; present for "
        "height_depth, depth_only, height_uv_depth."),
      =welder::mark::no_reassign]]
    std::vector<std::uint8_t> depthmap;

    [[=welder::doc(
        "UV texture coordinates, (width+1) x (height+1) row-major; present "
        "for height_uv and height_uv_depth."),
      =welder::mark::no_reassign]]
    std::vector<UVMapEntry> uvmap;

    bool operator==(const LiquidInstance&) const = default;

    /** The vertex count each present array holds. */
    std::size_t vertex_count() const {
      return static_cast<std::size_t>(width + 1) * static_cast<std::size_t>(height + 1);
    }
  };

  /** All liquid of one terrain cell: the attribute masks plus its layers. */
  struct [[
      =welder::weld,
      =welder::doc(R"(
        The liquid of one terrain cell (an MH2O chunk entry): the 8x8 fishable and
        deep attribute bit masks and the stacked liquid layers (instances). A cell
        with no liquid has no instances.)")
    ]] MapChunkLiquid {
    [[=welder::doc(
      "The 8x8 fishable bit mask (visibility); 0 when the cell omits "
      "attributes.")]]
    std::uint64_t fishable = 0;

    [[=welder::doc(
      "The 8x8 deep bit mask (fatigue water); 0 when the cell omits "
      "attributes.")]]
    std::uint64_t deep = 0;

    [[=welder::doc(
      "Whether this cell wrote an explicit attributes block (so an all-zero "
      "mask round-trips as present rather than omitted).")]]
    bool has_attributes = false;

    [[=welder::doc("The stacked liquid layers over this chunk."),
      =welder::mark::no_reassign]]
    std::vector<LiquidInstance> instances;

    bool operator==(const MapChunkLiquid&) const = default;

    /** @return whether the chunk carries any liquid. */
    [[nodiscard]]
    [[=welder::getter, =welder::doc("Whether the chunk carries any liquid.")]]
    bool empty() const { return instances.empty(); }
  };

  /** The whole-tile MH2O chunk: liquid for each of the 256 cells, decoded.
      SelfSerializing — it owns the offset-based MH2O payload layout. */
  struct [[
      =welder::weld,
      =welder::doc(R"(
        The tile's liquid (MH2O, WotLK+): one MapChunkLiquid per terrain cell, in
        the 16x16 row-major cell order. Decoded from the chunk's offset structure
        and re-laid on write (the binary offsets are derived). Index it by
        cell = y * 16 + x.)")
    ]] MH2OData {
    [[=welder::doc("Liquid for each of the 256 terrain chunks (y * 16 + x)."),
      =welder::mark::no_reassign]]
    std::vector<MapChunkLiquid> cells;

    /** Decode the whole-tile MH2O offset structure into the 256 per-chunk entries.
        @param payload the MH2O chunk payload.
        @return a structural error or success. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> payload);

    /** Re-lay the 256 entries into a fresh canonical MH2O payload (every binary
        offset derived), appended to @a out.
        @param out the destination buffer.
        @return a structural error or success. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const;

    [[nodiscard]]
    [[=welder::getter, =welder::doc("Whether the tile has no liquid at all.")]]
    bool empty() const {
      return std::ranges::none_of(cells, [](const MapChunkLiquid& c) {
        return !c.instances.empty();
      });
    }

    bool operator==(const MH2OData&) const = default;
  };

  /** The legacy per-chunk liquid (MCLQ, up to and including WotLK): a 9x9 vertex
      grid, an 8x8 tile-flag grid and up to two flow vectors, decoded.
      SelfSerializing. */
  struct [[
      =welder::weld,
      =welder::doc(R"(
        A terrain chunk's legacy liquid (MCLQ, up to WotLK, deprecated by MH2O): a
        9x9 grid of liquid vertices (SLVert; read as magma/slime via as_magma()),
        an 8x8 grid of tile flag bytes and the active flow vectors. The height
        range and the flow-vector count are stored; the trailing pair of flow
        vectors is always written.)")
    ]] MCLQData {
    [[=welder::doc("The surface height range (CRange: min, max).")]]
    CRange height_range{};

    [[=welder::doc("The 9x9 = 81 liquid vertices, row-major."),
      =welder::mark::no_reassign]]
    std::vector<SLVert> vertices;

    [[=welder::doc(
        "The 8x8 = 64 tile flag bytes (bits: liquid type, don't-render, "
        "fatigue)."),
      =welder::mark::no_reassign]]
    std::vector<std::uint8_t> tiles;

    [[=welder::doc("The active flow vectors (0-2)."),
      =welder::mark::no_reassign]]
    std::vector<SWFlowv> flows;

    /** Decode an MCLQ payload into the height range, 9x9 vertex grid, 8x8 tile
        flags and active flow vectors.
        @param payload the MCLQ chunk payload.
        @return a structural error or success. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> payload);

    /** Re-lay the legacy liquid into a fixed-grid MCLQ payload (81 verts, 64
        tiles, the two-slot flow pair), appended to @a out.
        @param out the destination buffer.
        @return a structural error or success. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const;

    /** @return whether the chunk carries no legacy liquid. */
    [[nodiscard]]
    [[=welder::getter, =welder::doc(
      "Whether the chunk carries no legacy liquid.")]]
    bool empty() const { return vertices.empty(); }

    bool operator==(const MCLQData&) const = default;
  };

  // --- MH2OData ---------------------------------------------------------------

  namespace detail {
    /** Resolve a liquid instance's vertex layout from its stored field and the
        span of its vertex-data block: the offset-multiplier method (wowdev),
        robust across every client. total_bytes is the vertex block's size,
        n the (width+1)*(height+1) vertex count. */
    inline LiquidVertexFormat resolve_vertex_format(std::uint16_t stored, std::size_t total_bytes, std::size_t n) {
      if (n != 0 && total_bytes % n == 0)
        switch (total_bytes / n) {
        case 5: return LiquidVertexFormat::height_depth; // float + byte
        case 8: return LiquidVertexFormat::height_uv; // float + 2 u16
        case 1: return LiquidVertexFormat::depth_only; // byte
        case 9: return LiquidVertexFormat::height_uv_depth;
        // float + 2 u16 + byte
        default: break;
        }
      // no vertex block (ocean): trust the stored LVF when small, else flat depth
      if (stored <= 3) return static_cast<LiquidVertexFormat>(stored);
      return LiquidVertexFormat::depth_only;
    }
  }

  inline Result<void> MH2OData::read(std::span<const std::byte> payload) {
    constexpr std::size_t header_bytes = 256 * 12;
    if (payload.size() < header_bytes)
      return make_error(ErrorCode::ChunkTruncated,
                        std::format("MH2O needs {} header bytes, got {}", header_bytes, payload.size()));
    cells.assign(256, MapChunkLiquid{});

    const auto u32_at = [&](std::size_t off) {
      std::uint32_t v = 0;
      std::memcpy(&v, payload.data() + off, 4);
      return v;
    };

    // Collect every data-block start offset first, so a block's size is the gap
    // to the next start (the vertex-format multiplier method needs the size).
    std::vector<std::size_t> starts;
    for (std::size_t c = 0; c < 256; ++c) {
      const std::uint32_t ofs_inst = u32_at(c * 12 + 0);
      const std::uint32_t count = u32_at(c * 12 + 4);
      const std::uint32_t ofs_attr = u32_at(c * 12 + 8);
      if (ofs_attr) starts.push_back(ofs_attr);
      for (std::uint32_t k = 0; k < count; ++k) {
        const std::size_t base = ofs_inst + 24 * k;
        if (base + 24 > payload.size())
          return make_error(ErrorCode::ChunkTruncated, std::format("MH2O instance of cell {} overruns the chunk", c));
        const std::uint32_t ofs_exists = u32_at(base + 16);
        const std::uint32_t ofs_verts = u32_at(base + 20);
        if (ofs_exists) starts.push_back(ofs_exists);
        if (ofs_verts) starts.push_back(ofs_verts);
      }
    }
    std::ranges::sort(starts);
    const auto block_end = [&](std::size_t start) -> std::size_t {
      std::size_t end = payload.size();
      for (std::size_t s : starts)
        if (s > start && s < end) end = s;
      return end;
    };

    for (std::size_t c = 0; c < 256; ++c) {
      const std::uint32_t ofs_inst = u32_at(c * 12 + 0);
      const std::uint32_t count = u32_at(c * 12 + 4);
      const std::uint32_t ofs_attr = u32_at(c * 12 + 8);
      MapChunkLiquid& cell = cells[c];
      if (ofs_attr) {
        if (ofs_attr + 16 > payload.size())
          return make_error(ErrorCode::ChunkTruncated, std::format("MH2O attributes of cell {} overrun", c));
        std::memcpy(&cell.fishable, payload.data() + ofs_attr, 8);
        std::memcpy(&cell.deep, payload.data() + ofs_attr + 8, 8);
        cell.has_attributes = true;
      }
      for (std::uint32_t k = 0; k < count; ++k) {
        const std::size_t base = ofs_inst + 24 * k;
        LiquidInstance inst;
        std::memcpy(&inst.liquid_type, payload.data() + base + 0, 2);
        std::memcpy(&inst.liquid_object_or_lvf, payload.data() + base + 2, 2);
        std::memcpy(&inst.min_height, payload.data() + base + 4, 4);
        std::memcpy(&inst.max_height, payload.data() + base + 8, 4);
        inst.x_offset = std::to_integer<std::uint8_t>(payload[base + 12]);
        inst.y_offset = std::to_integer<std::uint8_t>(payload[base + 13]);
        inst.width = std::to_integer<std::uint8_t>(payload[base + 14]);
        inst.height = std::to_integer<std::uint8_t>(payload[base + 15]);
        const std::uint32_t ofs_exists = u32_at(base + 16);
        const std::uint32_t ofs_verts = u32_at(base + 20);
        const std::size_t n = inst.vertex_count();

        if (ofs_exists) {
          const std::size_t bytes = (static_cast<std::size_t>(inst.width) * inst.height + 7) / 8;
          if (ofs_exists + bytes > payload.size())
            return make_error(ErrorCode::ChunkTruncated, std::format("MH2O exists mask of cell {} overruns", c));
          inst.exists_bitmap.assign(reinterpret_cast<const std::uint8_t*>(payload.data() + ofs_exists),
                                    reinterpret_cast<const std::uint8_t*>(payload.data() + ofs_exists + bytes));
        }

        if (ofs_verts) {
          const std::size_t total = block_end(ofs_verts) - ofs_verts;
          inst.vertex_format = detail::resolve_vertex_format(inst.liquid_object_or_lvf, total, n);
          std::size_t p = ofs_verts;
          const auto has_height = inst.vertex_format == LiquidVertexFormat::height_depth || inst.vertex_format ==
            LiquidVertexFormat::height_uv || inst.vertex_format == LiquidVertexFormat::height_uv_depth;
          const auto has_uv = inst.vertex_format == LiquidVertexFormat::height_uv || inst.vertex_format ==
            LiquidVertexFormat::height_uv_depth;
          const auto has_depth = inst.vertex_format == LiquidVertexFormat::height_depth || inst.vertex_format ==
            LiquidVertexFormat::depth_only || inst.vertex_format == LiquidVertexFormat::height_uv_depth;
          if (has_height) {
            inst.heightmap.resize(n);
            std::memcpy(inst.heightmap.data(), payload.data() + p, n * 4);
            p += n * 4;
          }
          if (has_uv) {
            inst.uvmap.resize(n);
            std::memcpy(inst.uvmap.data(), payload.data() + p, n * 4);
            p += n * 4;
          }
          if (has_depth) {
            inst.depthmap.assign(reinterpret_cast<const std::uint8_t*>(payload.data() + p),
                                 reinterpret_cast<const std::uint8_t*>(payload.data() + p + n));
            p += n;
          }
        }
        else {
          inst.vertex_format = detail::resolve_vertex_format(inst.liquid_object_or_lvf, 0, n);
        }
        cell.instances.push_back(std::move(inst));
      }
    }
    return {};
  }

  inline Result<void> MH2OData::write(FileBuffer& out) const {
    // Canonical relayout: [256 headers][instances per cell][per-cell data blocks].
    const std::size_t start = out.size();
    const std::size_t n_cells = cells.size() == 256 ? 256 : 256;
    out.resize(out.size() + 256 * 12, std::byte{0});

    const auto put_u32 = [&](std::uint32_t v) {
      const auto* b = reinterpret_cast<const std::byte*>(&v);
      out.insert(out.end(), b, b + 4);
    };
    const auto set_u32 = [&](std::size_t at, std::uint32_t v) {
      std::memcpy(out.data() + at, &v, 4);
    };
    const auto rel = [&](std::size_t abs) {
      return static_cast<std::uint32_t>(abs - start);
    };

    // instances first (so their offsets are known before data blocks)
    struct InstLoc {
      std::size_t header_at;
      std::size_t exists_at;
      std::size_t verts_at;
    };
    std::vector<std::vector<InstLoc>> locs(256);
    for (std::size_t c = 0; c < n_cells; ++c) {
      const MapChunkLiquid& cell = c < cells.size() ? cells[c] : MapChunkLiquid{};
      if (cell.instances.empty()) continue;
      set_u32(start + c * 12 + 0, rel(out.size()));
      set_u32(start + c * 12 + 4, static_cast<std::uint32_t>(cell.instances.size()));
      locs[c].resize(cell.instances.size());
      for (std::size_t k = 0; k < cell.instances.size(); ++k) {
        const LiquidInstance& inst = cell.instances[k];
        locs[c][k].header_at = out.size();
        put_u32(inst.liquid_type | (std::uint32_t{inst.liquid_object_or_lvf} << 16));
        put_u32(std::bit_cast<std::uint32_t>(inst.min_height));
        put_u32(std::bit_cast<std::uint32_t>(inst.max_height));
        out.push_back(std::byte{inst.x_offset});
        out.push_back(std::byte{inst.y_offset});
        out.push_back(std::byte{inst.width});
        out.push_back(std::byte{inst.height});
        put_u32(0); // offset_exists placeholder
        put_u32(0); // offset_vertex placeholder
      }
    }

    // per-cell data: attributes, then each instance's exists mask + vertex data
    for (std::size_t c = 0; c < n_cells; ++c) {
      const MapChunkLiquid& cell = c < cells.size() ? cells[c] : MapChunkLiquid{};
      if (cell.has_attributes) {
        set_u32(start + c * 12 + 8, rel(out.size()));
        put_u32(static_cast<std::uint32_t>(cell.fishable & 0xFFFFFFFF));
        put_u32(static_cast<std::uint32_t>(cell.fishable >> 32));
        put_u32(static_cast<std::uint32_t>(cell.deep & 0xFFFFFFFF));
        put_u32(static_cast<std::uint32_t>(cell.deep >> 32));
      }
      for (std::size_t k = 0; k < cell.instances.size(); ++k) {
        const LiquidInstance& inst = cell.instances[k];
        const InstLoc& loc = locs[c][k];
        if (!inst.exists_bitmap.empty()) {
          set_u32(loc.header_at + 16, rel(out.size()));
          const auto* b = reinterpret_cast<const std::byte*>(inst.exists_bitmap.data());
          out.insert(out.end(), b, b + inst.exists_bitmap.size());
        }
        const bool any_verts = !inst.heightmap.empty() || !inst.depthmap.empty() || !inst.uvmap.empty();
        if (any_verts) {
          set_u32(loc.header_at + 20, rel(out.size()));
          if (!inst.heightmap.empty()) {
            const auto* b = reinterpret_cast<const std::byte*>(inst.heightmap.data());
            out.insert(out.end(), b, b + inst.heightmap.size() * 4);
          }
          if (!inst.uvmap.empty()) {
            const auto* b = reinterpret_cast<const std::byte*>(inst.uvmap.data());
            out.insert(out.end(), b, b + inst.uvmap.size() * 4);
          }
          if (!inst.depthmap.empty()) {
            const auto* b = reinterpret_cast<const std::byte*>(inst.depthmap.data());
            out.insert(out.end(), b, b + inst.depthmap.size());
          }
        }
      }
    }
    return {};
  }

  // --- MCLQData ---------------------------------------------------------------

  inline Result<void> MCLQData::read(std::span<const std::byte> payload) {
    // CRange height (8) + 81 verts (8 each = 648) + 64 tile bytes + u32 nFlowvs
    // + 2 flow vectors (40 each). The trailing flow pair is always present.
    constexpr std::size_t fixed = 8 + 81 * 8 + 64 + 4;
    // An "empty" MCLQ (the MCNK header's size_liquid was <= 8, so the sub-chunk
    // carries no record) decodes to no liquid — Outland WotLK tiles ship these.
    if (payload.size() < fixed) return {};
    std::memcpy(&height_range, payload.data(), 8);
    vertices.resize(81);
    std::memcpy(vertices.data(), payload.data() + 8, 81 * 8);
    tiles.assign(reinterpret_cast<const std::uint8_t*>(payload.data() + 8 + 648),
                 reinterpret_cast<const std::uint8_t*>(payload.data() + 8 + 648 + 64));
    std::uint32_t n_flows = 0;
    std::memcpy(&n_flows, payload.data() + 8 + 648 + 64, 4);
    std::size_t p = fixed;
    flows.clear();
    for (std::uint32_t i = 0; i < 2 && p + 40 <= payload.size(); ++i, p += 40) {
      if (i < n_flows) {
        SWFlowv f;
        std::memcpy(&f, payload.data() + p, 40);
        flows.push_back(f);
      }
    }
    return {};
  }

  inline Result<void> MCLQData::write(FileBuffer& out) const {
    const auto put = [&](const void* p, std::size_t n) {
      const auto* b = static_cast<const std::byte*>(p);
      out.insert(out.end(), b, b + n);
    };
    put(&height_range, 8);
    // exactly 81 verts / 64 tiles are written; pad or truncate to the fixed grid
    std::vector<SLVert> v = vertices;
    v.resize(81);
    put(v.data(), 81 * 8);
    std::vector<std::uint8_t> t = tiles;
    t.resize(64);
    put(t.data(), 64);
    const std::uint32_t n_flows = static_cast<std::uint32_t>(flows.size());
    put(&n_flows, 4);
    SWFlowv pair[2]{};
    for (std::size_t i = 0; i < flows.size() && i < 2; ++i) pair[i] = flows[i];
    put(pair, 80);
    return {};
  }
}
