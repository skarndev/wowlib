#pragma once

/** @file
    WMO group liquid (MLIQ) (namespace wowlib::formats::wmo::group::chunks). The
    MLIQ payload is an intra-chunk offset structure — a 30-byte header whose
    fields drive the lengths of two trailing arrays — so it cannot be a plain
    data/array chunk; MLIQData owns its payload encoding (SelfSerializing) the
    way ChunkBlob and StringBlock do. */

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
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo::group::chunks {
  // --- MLIQ -------------------------------------------------------------------

  struct [[
      =welder::weld,
      =welder::doc(R"(
        The magma/slime reading of an MLIQ vertex (8 bytes): two int16 texture
        coordinates sharing the layout with the water reading (SMOLVert). Obtain
        one with SMOLVert.as_magma(); which reading is correct is set by the
        group's liquid type, not stored per vertex.)")
    ]] SMOMVert {
    [[=welder::doc("Texture coordinate s (the water flow1/flow2 bytes).")]]
    std::int16_t s = 0;

    [[=welder::doc("Texture coordinate t (the water flow1_pct/filler bytes).")]]
    std::int16_t t = 0;

    [[=welder::doc("Liquid surface height, shared with the water reading.")]]
    float height = 0;
  };

  static_assert(sizeof(SMOMVert) == 0x8);

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MLIQ liquid vertex (8 bytes). The byte layout is fixed but its
        interpretation depends on the group's liquid type: water/ocean read the
        named flow fields; magma/slime reinterpret the first four bytes as two
        int16 texture coordinates (s, t) — use as_magma()/set_magma() for that
        reading. Both are 8 bytes, so round-trip is layout-agnostic — pick the
        reading from the group's liquid type.)")
    ]] SMOLVert {
    [[=welder::doc("Water: flow velocity 1. Magma/slime: low byte of int16 s.")]
    ]
    std::uint8_t flow1 = 0;

    [[=welder::doc("Water: flow velocity 2. Magma/slime: high byte of int16 s.")
    ]]
    std::uint8_t flow2 = 0;

    [[=welder::doc(
      "Water: flow-1 percentage. Magma/slime: low byte of int16 t.")]]
    std::uint8_t flow1_pct = 0;

    [[=welder::doc("Water: filler. Magma/slime: high byte of int16 t.")]]
    std::uint8_t filler = 0;

    [[=welder::doc("Liquid surface height at this vertex.")]]
    float height = 0;

    [[nodiscard]]
    [[=welder::doc("Reinterpret this vertex under the magma/slime reading "
      "(int16 s, t texcoords). A pure byte reinterpretation; use "
      "it only when the group's liquid type is magma or slime.")]]
    SMOMVert as_magma() const { return std::bit_cast<SMOMVert>(*this); }

    [[=welder::doc("Overwrite this vertex's bytes from a magma/slime reading.")]
    ]
    void set_magma(const SMOMVert& magma) {
      *this = std::bit_cast<SMOLVert>(magma);
    }
  };

  static_assert(sizeof(SMOLVert) == 0x8);

  struct [[
      =welder::weld,
      =welder::doc("One MLIQ tile flag byte: bits 0-5 the legacy liquid type, "
        "bit 6 fishable, bit 7 shared with an adjacent group.")
    ]] SMOLTile {
    [[=welder::doc("Packed liquid-type / fishable / shared flags.")]]
    std::uint8_t flags = 0;
  };

  static_assert(sizeof(SMOLTile) == 0x1);

  struct [[
      =welder::weld,
      =welder::doc(R"(
        Structured MLIQ liquid data: a vertex grid and a tile-flag grid with a
        base position and material id. The on-disk header is 30 bytes (the
        uint16 material id sits directly after a 12-byte vector, so the header
        is read field-by-field, not memcpy'd) followed by verts_dim.x*verts_dim.y
        vertices and tiles_dim.x*tiles_dim.y tile bytes.)")
    ]] MLIQData {
    [[=welder::doc("Vertex grid dimensions (xverts, yverts).")]]
    C2iVector verts_dim{};

    [[=welder::doc("Tile grid dimensions (xtiles, ytiles); one less than the "
      "vertex dimensions in each axis.")]]
    C2iVector tiles_dim{};

    [[=welder::doc("Grid origin in group space.")]]
    C3Vector base_coords{};

    [[=welder::doc("Liquid material id: an MOMT index, or a LiquidType id when "
      "the root's use_liquid_type_dbc_id flag is set.")]]
    std::uint16_t material_id = 0;

    [[=welder::doc("verts_dim.x * verts_dim.y liquid vertices, row-major."),
      =welder::mark::no_reassign]]
    std::vector<SMOLVert> vertices;

    [[=welder::doc("tiles_dim.x * tiles_dim.y tile flag bytes, row-major."),
      =welder::mark::no_reassign]]
    std::vector<SMOLTile> tiles;

    /** The on-disk header size: four 4-byte ints, a 12-byte vector and a 2-byte
        material id, with no trailing padding — unlike sizeof(a natural struct),
        which would pad the material id up to a 4-byte boundary. */
    static constexpr std::size_t header_size = 30;

    /** Decode the MLIQ payload (the serializer's read hook). Reads the 30-byte
        header field-by-field, derives the two array lengths from it and copies
        the vertex and tile grids.
        @param payload the chunk payload bytes.
        @return an error if the payload is too short for the header or the grids
                it describes; success otherwise. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> payload) {
      if (payload.size() < header_size)
        return make_error(ErrorCode::ChunkTruncated,
                          std::format("MLIQ header needs {} bytes, got {}",
                                      header_size, payload.size()));

      std::memcpy(&verts_dim, payload.data() + 0, sizeof verts_dim);
      std::memcpy(&tiles_dim, payload.data() + 8, sizeof tiles_dim);
      std::memcpy(&base_coords, payload.data() + 16, sizeof base_coords);
      std::memcpy(&material_id, payload.data() + 28, sizeof material_id);

      const std::size_t n_verts =
        static_cast<std::size_t>(verts_dim.x) * static_cast<std::size_t>(
          verts_dim.y);
      const std::size_t n_tiles =
        static_cast<std::size_t>(tiles_dim.x) * static_cast<std::size_t>(
          tiles_dim.y);
      const std::size_t need =
        header_size + n_verts * sizeof(SMOLVert) + n_tiles * sizeof(SMOLTile);
      if (payload.size() < need)
        return make_error(ErrorCode::ChunkTruncated,
                          std::format("MLIQ body needs {} bytes ({} verts, {} "
                                      "tiles), got {}",
                                      need, n_verts, n_tiles, payload.size()));

      vertices.resize(n_verts);
      std::memcpy(vertices.data(), payload.data() + header_size,
                  n_verts * sizeof(SMOLVert));
      tiles.resize(n_tiles);
      std::memcpy(tiles.data(),
                  payload.data() + header_size + n_verts * sizeof(SMOLVert),
                  n_tiles * sizeof(SMOLTile));
      return {};
    }

    /** Encode the MLIQ payload (the serializer's write hook): the 30-byte
        header field-by-field, then the vertex and tile grids verbatim.
        @param out the destination buffer (appended, not cleared).
        @return nothing; emitting cannot fail. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const {
      const auto put = [&out](const auto& field) {
        const auto* b = reinterpret_cast<const std::byte*>(&field);
        out.insert(out.end(), b, b + sizeof field);
      };
      put(verts_dim);
      put(tiles_dim);
      put(base_coords);
      put(material_id);

      const auto* vb = reinterpret_cast<const std::byte*>(vertices.data());
      out.insert(out.end(), vb, vb + vertices.size() * sizeof(SMOLVert));
      const auto* tb = reinterpret_cast<const std::byte*>(tiles.data());
      out.insert(out.end(), tb, tb + tiles.size() * sizeof(SMOLTile));
      return {};
    }

    [[nodiscard]]
    [[=welder::getter, =welder::doc(
      "Whether this group carries no liquid grid.")]]
    bool empty() const { return vertices.empty(); }
  };
}
