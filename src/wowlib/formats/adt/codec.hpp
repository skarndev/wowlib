#pragma once

/** @file
    The MCAL alpha-map and MCSH shadow-map codecs (namespace
    wowlib::formats::adt::detail): AlphaMapCodec and ShadowMapCodec.

    wowlib stores terrain alpha and shadow maps as a plain 64x64 8-bit / 1-byte
    edit surface (4096 bytes), regardless of how they sit on disk. On disk an
    alpha map is one of three encodings — 2048-byte 4-bit, 4096-byte 8-bit, or
    RLE-compressed 8-bit — chosen by the tile's AlphaFormat and the layer's
    compressed flag; a shadow map is a 512-byte 1-bit bitmap. Both may be stored
    in the "unfixed" 63x63 form the client repairs on load (MCNK flag
    do_not_fix_alpha clear); wowlib fixes them to the full 64x64 on read and
    always writes the fixed form (setting the flag), which makes the fix
    idempotent under a re-read — the semantic-round-trip contract.

    Both codecs are classes rather than free functions: encoding a terrain map is
    a first-class operation with several private encoding strategies, so it earns
    an object with a small public decode()/encode() surface and the per-encoding
    routines as protected members. The on-disk bit depth (AlphaFormat) is the
    codec's state; the per-layer compression and edge-fix choices are call
    parameters. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <wowlib/formats/adt/boundaries.hpp>

namespace wowlib::formats::adt::detail
{
  /** The edge length of the decoded square edit surface (64). */
  inline constexpr std::size_t alpha_dim = 64;
  /** The texel count of a decoded map (64 * 64 = 4096). */
  inline constexpr std::size_t alpha_texels = alpha_dim * alpha_dim;
  /** The on-disk size of a 4-bit alpha map (4096 / 2 = 2048 bytes). */
  inline constexpr std::size_t alpha_4bit_bytes = alpha_texels / 2;
  /** The on-disk size of a 1-bit shadow bitmap (4096 / 8 = 512 bytes). */
  inline constexpr std::size_t shadow_bytes = alpha_texels / 8;

  /** Shared geometry for the 64x64 terrain map codecs: the last-row/column
      repair the client applies to "unfixed" maps on load. Not instantiated on
      its own — AlphaMapCodec and ShadowMapCodec derive from it. */
  class TerrainMapCodec
  {
  protected:
    /** Repair an "unfixed" map in place: the last row and column are ignored on
        disk and the client copies their neighbours. Applied on read when the
        MCNK do_not_fix_alpha flag is clear, so wowlib always holds the full grid.
        @param map the 4096-texel edit surface to repair in place. */
    static void fix_last_row_col(std::span<std::uint8_t> map)
    {
      for (std::size_t r = 0; r < alpha_dim; ++r)
        map[r * alpha_dim + 63] = map[r * alpha_dim + 62];
      for (std::size_t c = 0; c < alpha_dim; ++c)
        map[63 * alpha_dim + c] = map[62 * alpha_dim + c];
    }
  };

  /** The MCAL alpha-map codec. Constructed with the tile's on-disk bit depth
      (its state); decode()/encode() move a single layer between its on-disk bytes
      and the 4096-texel edit surface, choosing the encoding from that depth and
      the per-layer compression flag. */
  class AlphaMapCodec : TerrainMapCodec
  {
  public:
    /** @param format the tile's on-disk alpha bit depth (from the WDT MPHD). */
    explicit AlphaMapCodec(AlphaFormat format) noexcept : format_{format} {}

    /** Decode one layer's on-disk alpha bytes to the 4096-texel edit surface.
        @param src        the layer's on-disk alpha bytes.
        @param compressed whether the layer is RLE-compressed (MCLY flag 0x200);
                          overrides the bit depth when set.
        @param fix_edges  whether to repair the 63x63 "unfixed" form (MCNK flag
                          do_not_fix_alpha clear).
        @return the decoded 4096-texel map. */
    [[nodiscard]]
    std::vector<std::uint8_t> decode(std::span<const std::byte> src, bool compressed,
                                     bool fix_edges) const
    {
      std::vector<std::uint8_t> map;
      if (compressed)
        decode_rle(src, map);
      else if (format_ == AlphaFormat::highres_8bit)
        decode_8bit(src, map);
      else
        decode_4bit(src, map);
      if (fix_edges)
        fix_last_row_col(map);
      return map;
    }

    /** Encode a 4096-texel edit surface to its on-disk alpha bytes, appended to
        @a out. wowlib always holds and re-emits the fixed 64x64 form.
        @param map        the 4096-texel edit surface.
        @param compressed whether to RLE-compress (MCLY flag 0x200).
        @param out        the destination byte buffer, appended to. */
    void encode(std::span<const std::uint8_t> map, bool compressed,
                std::vector<std::byte>& out) const
    {
      if (compressed)
        encode_rle(map, out);
      else if (format_ == AlphaFormat::highres_8bit)
        encode_8bit(map, out);
      else
        encode_4bit(map, out);
    }

  protected:
    /** Decode a 2048-byte 4-bit map to 4096 texels (nibble * 0x11, LSB first).
        @param src the on-disk 4-bit bytes.
        @param out the decoded surface, resized to 4096. */
    void decode_4bit(std::span<const std::byte> src, std::vector<std::uint8_t>& out) const
    {
      out.resize(alpha_texels);
      for (std::size_t i = 0; i < alpha_4bit_bytes && i < src.size(); ++i)
      {
        const auto b = std::to_integer<std::uint8_t>(src[i]);
        out[2 * i] = static_cast<std::uint8_t>((b & 0x0F) * 0x11);
        out[2 * i + 1] = static_cast<std::uint8_t>((b >> 4) * 0x11);
      }
    }

    /** Decode a 4096-byte 8-bit map (a straight copy).
        @param src the on-disk 8-bit bytes.
        @param out the decoded surface, resized to 4096. */
    void decode_8bit(std::span<const std::byte> src, std::vector<std::uint8_t>& out) const
    {
      out.resize(alpha_texels);
      for (std::size_t i = 0; i < alpha_texels && i < src.size(); ++i)
        out[i] = std::to_integer<std::uint8_t>(src[i]);
    }

    /** Decode a Blizzard RLE map (8-bit) to 4096 texels. Tolerates the known
        corrupt chunks that unpack past 4096 by stopping at the boundary.
        @param src the on-disk RLE bytes.
        @param out the decoded surface, resized to 4096. */
    void decode_rle(std::span<const std::byte> src, std::vector<std::uint8_t>& out) const
    {
      out.clear();
      out.reserve(alpha_texels);
      std::size_t p = 0;
      while (out.size() < alpha_texels && p < src.size())
      {
        const auto control = std::to_integer<std::uint8_t>(src[p++]);
        const std::size_t count = control & 0x7F;
        if (control & 0x80)  // fill
        {
          if (p >= src.size())
            break;
          const auto value = std::to_integer<std::uint8_t>(src[p++]);
          for (std::size_t k = 0; k < count && out.size() < alpha_texels; ++k)
            out.push_back(value);
        }
        else  // copy
        {
          for (std::size_t k = 0; k < count && out.size() < alpha_texels && p < src.size(); ++k)
            out.push_back(std::to_integer<std::uint8_t>(src[p++]));
        }
      }
      out.resize(alpha_texels);
    }

    /** Encode 4096 texels to a 2048-byte 4-bit map (texel >> 4).
        @param map the 4096-texel surface.
        @param out the destination buffer, appended to. */
    void encode_4bit(std::span<const std::uint8_t> map, std::vector<std::byte>& out) const
    {
      for (std::size_t i = 0; i < alpha_4bit_bytes; ++i)
      {
        const auto lo = static_cast<std::uint8_t>(map[2 * i] >> 4);
        const auto hi = static_cast<std::uint8_t>(map[2 * i + 1] >> 4);
        out.push_back(static_cast<std::byte>(lo | (hi << 4)));
      }
    }

    /** Encode 4096 texels to a 4096-byte 8-bit map (a straight copy).
        @param map the 4096-texel surface.
        @param out the destination buffer, appended to. */
    void encode_8bit(std::span<const std::uint8_t> map, std::vector<std::byte>& out) const
    {
      for (std::uint8_t v : map)
        out.push_back(static_cast<std::byte>(v));
    }

    /** RLE-encode 4096 texels one row at a time (runs cannot span rows). A greedy
        encoder: fill runs of >= 3 equal bytes, copy otherwise. This does NOT
        reproduce Blizzard's exact byte stream (their encoder differs), which is
        why ADT round-trip is semantic, not byte-identical.
        @param map the 4096-texel surface.
        @param out the destination buffer, appended to. */
    void encode_rle(std::span<const std::uint8_t> map, std::vector<std::byte>& out) const
    {
      for (std::size_t row = 0; row < alpha_dim; ++row)
      {
        const std::uint8_t* r = map.data() + row * alpha_dim;
        std::size_t i = 0;
        std::size_t copy_start = alpha_dim + 1;  // "none"
        const auto flush_copy = [&](std::size_t end) {
          if (copy_start > alpha_dim)
            return;
          out.push_back(static_cast<std::byte>(end - copy_start));  // copy, count
          for (std::size_t k = copy_start; k < end; ++k)
            out.push_back(static_cast<std::byte>(r[k]));
          copy_start = alpha_dim + 1;
        };
        while (i < alpha_dim)
        {
          std::size_t run = 1;
          while (i + run < alpha_dim && r[i + run] == r[i])
            ++run;
          if (run >= 3)
          {
            flush_copy(i);
            out.push_back(static_cast<std::byte>(0x80 | run));  // fill
            out.push_back(static_cast<std::byte>(r[i]));
            i += run;
          }
          else
          {
            if (copy_start > alpha_dim)
              copy_start = i;
            ++i;
          }
        }
        flush_copy(alpha_dim);
      }
    }

  private:
    /** The tile's on-disk alpha bit depth. */
    AlphaFormat format_;
  };

  /** The MCSH shadow-map codec: a 512-byte 1-bit bitmap on disk, a 4096-texel
      0/1 surface in memory. Stateless (a shadow map has no bit-depth axis), but a
      class for symmetry with AlphaMapCodec and to keep the encoding routines off
      the free-function surface. */
  class ShadowMapCodec : TerrainMapCodec
  {
  public:
    /** Decode a 512-byte 1-bit shadow bitmap to 4096 texels (0/1), LSB first.
        @param src       the on-disk 1-bit bytes.
        @param fix_edges whether to repair the 63x63 "unfixed" form.
        @return the decoded 4096-texel map. */
    [[nodiscard]]
    std::vector<std::uint8_t> decode(std::span<const std::byte> src, bool fix_edges) const
    {
      std::vector<std::uint8_t> map(alpha_texels, 0);
      for (std::size_t i = 0; i < alpha_texels && i / 8 < src.size(); ++i)
      {
        const auto b = std::to_integer<std::uint8_t>(src[i / 8]);
        map[i] = (b >> (i % 8)) & 1;
      }
      if (fix_edges)
        fix_last_row_col(map);
      return map;
    }

    /** Encode 4096 texels (0/1) to a 512-byte 1-bit bitmap, LSB first.
        @param map the 4096-texel surface.
        @param out the destination buffer, appended to. */
    void encode(std::span<const std::uint8_t> map, std::vector<std::byte>& out) const
    {
      for (std::size_t byte = 0; byte < shadow_bytes; ++byte)
      {
        std::uint8_t packed = 0;
        for (std::size_t bit = 0; bit < 8; ++bit)
          if (map[byte * 8 + bit] & 1)
            packed |= static_cast<std::uint8_t>(1 << bit);
        out.push_back(static_cast<std::byte>(packed));
      }
    }
  };
}
