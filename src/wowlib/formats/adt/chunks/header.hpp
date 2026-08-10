#pragma once

/** @file
    ADT header binary structs (namespace wowlib::formats::adt::chunks): the
    per-cell MCNK header (SMChunk, 128 bytes) and the flying bounding box (MFBO).

    SMChunk carries many fields that are DERIVED from the cell's actual content —
    the sub-chunk offsets and sizes, the layer/ref/emitter counts. wowlib stamps
    those on write (MapChunk::write_slice) and hides them from the bindings
    (mark::exclude); the meaningful, user-authored fields (flags, indices, area,
    holes, the detail-doodad maps, the cell origin) stay exposed. */

#include <array>
#include <bit>
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::adt::chunks
{
  enum class [[
    =welder::weld,
    =welder::doc("Per-cell MCNK header flag bits (SMChunk.flags).")
  ]] MapChunkFlags : std::uint32_t
  {
    has_mcsh [[=welder::doc("The cell has a baked shadow map (MCSH sub-chunk).")]] = 0x1,
    impassable [[=welder::doc("Impassable terrain (no walking).")]] = 0x2,
    lq_river [[=welder::doc("Legacy (MCLQ) river liquid present.")]] = 0x4,
    lq_ocean [[=welder::doc("Legacy (MCLQ) ocean liquid present.")]] = 0x8,
    lq_magma [[=welder::doc("Legacy (MCLQ) magma liquid present.")]] = 0x10,
    lq_slime [[=welder::doc("Legacy (MCLQ) slime liquid present.")]] = 0x20,
    has_mccv [[=welder::doc("The cell has vertex colors (MCCV sub-chunk, WotLK+).")]] = 0x40,
    unknown_0x80 [[=welder::doc("Unknown (0x80).")]] = 0x80,
    do_not_fix_alpha_map [[=welder::doc(R"(The MCAL/MCSH maps are stored in their full
                                          64x64 form rather than the 63x63 "unfixed"
                                          form the client repairs on load. wowlib always
                                          stores the full 64x64 edit surface and sets this
                                          flag on write.)")]] = 0x8000,
    high_res_holes [[=welder::doc(R"(The cell uses the 64-bit high-resolution hole mask
                                     (stored in the 8 bytes at header offset 0x14, ~5.3+)
                                     instead of the 16-bit holes_low_res field.)")]] = 0x10000
  };

  /** The per-cell MCNK header — 128 bytes, memcpy'd verbatim. Sub-chunk offsets
      and sizes and the derived counts are stamped by the serializer and hidden
      from the bindings; author the flags, indices, area, holes, detail-doodad
      maps and origin. */
  struct [[
    =welder::weld,
    =welder::weld_as("SMChunk"),
    =welder::doc(R"(
        A terrain cell header (MCNK, 128 bytes): the flags, its (x, y) position in
        the 16x16 cell grid, the area id, the terrain hole mask and the origin.
        The sub-chunk offset/size/count fields are binary bookkeeping wowlib derives
        on write, so they are not exposed. See https://wowdev.wiki/ADT/v18#MCNK_chunk.)")
  ]] SMChunk
  {
    [[=welder::doc("Flags; MapChunkFlags bits.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("The cell's column index in the 16x16 grid (0-15).")]]
    std::uint32_t index_x = 0;

    [[=welder::doc("The cell's row index in the 16x16 grid (0-15).")]]
    std::uint32_t index_y = 0;

    [[=welder::mark::exclude]] std::uint32_t n_layers = 0;
    [[=welder::mark::exclude]] std::uint32_t n_doodad_refs = 0;
    // 0x14/0x18: pre-5.3 the derived MCVT/MCNR offsets; with the high_res_holes
    // flag these 8 bytes are the 64-bit hole mask instead (see holes_high_res()).
    [[=welder::mark::exclude]] std::uint32_t ofs_height = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_normal = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_layer = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_refs = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_alpha = 0;
    [[=welder::mark::exclude]] std::uint32_t size_alpha = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_shadow = 0;
    [[=welder::mark::exclude]] std::uint32_t size_shadow = 0;

    [[=welder::doc("The cell's area id (a foreign key into AreaTable; in the alpha "
                   "clients, packed zone/subzone u16s).")]]
    std::uint32_t area_id = 0;

    [[=welder::mark::exclude]] std::uint32_t n_map_obj_refs = 0;

    [[=welder::doc("The 16-bit low-resolution terrain hole mask (a 4x4 bitfield); used "
                   "unless the high_res_holes flag selects holes_high_res.")]]
    std::uint16_t holes_low_res = 0;

    [[=welder::doc("Unknown but written (padding in the alpha clients).")]]
    std::uint16_t unknown_but_used = 0;

    [[=welder::doc(R"(The "really low quality" predominant-texture map: an 8x8 grid of
                      2-bit layer indices (16 bytes), naming the detail doodads to
                      show per sub-cell.)")]]
    std::array<std::uint8_t, 16> predominant_texture{};

    [[=welder::doc("An 8x8 bitfield (8 bytes): 1 disables detail doodads in that "
                   "sub-cell (WoD may store it as an explicit MCDD chunk).")]]
    std::array<std::uint8_t, 8> no_effect_doodad{};

    [[=welder::mark::exclude]] std::uint32_t ofs_snd_emitters = 0;
    [[=welder::mark::exclude]] std::uint32_t n_snd_emitters = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_liquid = 0;
    [[=welder::mark::exclude]] std::uint32_t size_liquid = 0;

    [[=welder::doc("The cell origin in world space; MCVT heights are relative to it.")]]
    C3Vector position{};

    [[=welder::mark::exclude]] std::uint32_t ofs_mccv = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mclv = 0;
    [[=welder::mark::exclude]] std::uint32_t unused = 0;

    [[nodiscard]]
    [[=welder::getter,
      =welder::doc("The 64-bit high-resolution hole mask (the 8 bytes at header offset "
                   "0x14), valid when the high_res_holes flag is set.")]]
    std::uint64_t holes_high_res() const
    {
      return (static_cast<std::uint64_t>(ofs_normal) << 32) | ofs_height;
    }

    [[=welder::doc("Set the 64-bit high-resolution hole mask (and the high_res_holes "
                   "flag).")]]
    void set_holes_high_res(std::uint64_t holes)
    {
      ofs_height = static_cast<std::uint32_t>(holes & 0xFFFFFFFF);
      ofs_normal = static_cast<std::uint32_t>(holes >> 32);
      flags |= static_cast<std::uint32_t>(MapChunkFlags::high_res_holes);
    }

    bool operator==(const SMChunk&) const = default;
  };
  static_assert(sizeof(SMChunk) == 0x80);

  enum class [[
    =welder::weld,
    =welder::doc("Tile header flag bits (MHDRData.flags).")
  ]] MapHeaderFlags : std::uint32_t
  {
    has_mfbo [[=welder::doc("The tile has a flying bounds chunk (MFBO).")]] = 0x1,
    northrend [[=welder::doc("Set on some Northrend tiles; meaning unknown.")]] = 0x2
  };

  /** The tile header (MHDR, 64 bytes, root/monolithic). All eleven chunk offset
      fields are DERIVED (stamped on write from the final layout) and hidden; the
      flags and the Cata+ MAMP override are the authored content. */
  struct [[
    =welder::weld,
    =welder::weld_as("MHDRData"),
    =welder::doc(R"(
        The tile header (MHDR, 64 bytes): flags plus offsets to the tile's other
        chunks. wowlib derives every offset from the written layout, so only the
        flags and the Cata+ inline MAMP alpha-downscale value are exposed. See
        https://wowdev.wiki/ADT/v18#MHDR_chunk.)")
  ]] MHDRData
  {
    [[=welder::doc("Flags; MapHeaderFlags bits.")]]
    std::uint32_t flags = 0;

    [[=welder::mark::exclude]] std::uint32_t ofs_mcin = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mtex = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mmdx = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mmid = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mwmo = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mwid = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mddf = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_modf = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mfbo = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mh2o = 0;
    [[=welder::mark::exclude]] std::uint32_t ofs_mtxf = 0;

    [[=welder::doc("The inline MAMP alpha-map downscale value (Cata+): the alpha texture "
                   "size is 64 / (2^value); an explicit MAMP chunk overrides it.")]]
    std::uint8_t mamp_value = 0;

    [[=welder::mark::exclude]] std::array<std::uint8_t, 3> padding{};
    [[=welder::mark::exclude]] std::array<std::uint32_t, 3> unused{};

    bool operator==(const MHDRData&) const = default;
  };
  static_assert(sizeof(MHDRData) == 0x40);

  /** The flying bounding box (MFBO, BC+): two 3x3 grids of int16 heights — a
      maximum plane you fall through and a minimum plane below the world. */
  struct [[
    =welder::weld,
    =welder::doc(R"(
        The flying bounds (MFBO, BC+): a maximum and a minimum plane, each a 3x3
        grid of int16 heights, limiting where a player may fly on this tile.)")
  ]] MFBOPlanes
  {
    [[=welder::doc("The maximum plane: 3x3 int16 heights (row-major).")]]
    std::array<std::int16_t, 9> maximum{};

    [[=welder::doc("The minimum plane: 3x3 int16 heights (row-major).")]]
    std::array<std::int16_t, 9> minimum{};

    bool operator==(const MFBOPlanes&) const = default;
  };
  static_assert(sizeof(MFBOPlanes) == 36);
}
