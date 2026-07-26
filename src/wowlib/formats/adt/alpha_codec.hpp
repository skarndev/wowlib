#pragma once

/** @file
    The MCAL alpha-map and MCSH shadow-map codecs (namespace
    wowlib::formats::adt::detail).

    wowlib stores terrain alpha and shadow maps as a plain 64x64 8-bit / 1-byte
    edit surface (4096 bytes), regardless of how they sit on disk. On disk an
    alpha map is one of three encodings — 2048-byte 4-bit, 4096-byte 8-bit, or
    RLE-compressed 8-bit — chosen by the tile's AlphaFormat and the layer's
    compressed flag; a shadow map is a 512-byte 1-bit bitmap. Both may be stored
    in the "unfixed" 63x63 form the client repairs on load (MCNK flag
    do_not_fix_alpha clear); wowlib fixes them to the full 64x64 on read and
    always writes the fixed form (setting the flag), which makes the fix
    idempotent under a re-read — the semantic-round-trip contract. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wowlib::formats::adt::detail
{
  inline constexpr std::size_t alpha_dim = 64;
  inline constexpr std::size_t alpha_texels = alpha_dim * alpha_dim;  // 4096
  inline constexpr std::size_t alpha_4bit_bytes = alpha_texels / 2;   // 2048
  inline constexpr std::size_t shadow_bytes = alpha_texels / 8;       // 512

  /** Repair an "unfixed" 64x64 map: the last row and column are ignored on disk
      and the client copies their neighbours. Applied on read when the MCNK
      do_not_fix_alpha flag is clear, so wowlib always holds the full grid. */
  inline void fix_last_row_col(std::vector<std::uint8_t>& map)
  {
    for (std::size_t r = 0; r < alpha_dim; ++r)
      map[r * alpha_dim + 63] = map[r * alpha_dim + 62];
    for (std::size_t c = 0; c < alpha_dim; ++c)
      map[63 * alpha_dim + c] = map[62 * alpha_dim + c];
  }

  /** Decode a 2048-byte 4-bit alpha map to 4096 8-bit texels (nibble * 0x11,
      LSB nibble first). */
  inline void decode_alpha_4bit(std::span<const std::byte> src, std::vector<std::uint8_t>& out)
  {
    out.resize(alpha_texels);
    for (std::size_t i = 0; i < alpha_4bit_bytes && i < src.size(); ++i)
    {
      const auto b = std::to_integer<std::uint8_t>(src[i]);
      out[2 * i] = static_cast<std::uint8_t>((b & 0x0F) * 0x11);
      out[2 * i + 1] = static_cast<std::uint8_t>((b >> 4) * 0x11);
    }
  }

  /** Decode a 4096-byte 8-bit alpha map (a straight copy). */
  inline void decode_alpha_8bit(std::span<const std::byte> src, std::vector<std::uint8_t>& out)
  {
    out.resize(alpha_texels);
    for (std::size_t i = 0; i < alpha_texels && i < src.size(); ++i)
      out[i] = std::to_integer<std::uint8_t>(src[i]);
  }

  /** Decode a Blizzard RLE alpha map (8-bit) to 4096 texels. Tolerates the known
      corrupted chunks that unpack past 4096 by stopping at the boundary. */
  inline void decode_alpha_rle(std::span<const std::byte> src, std::vector<std::uint8_t>& out)
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

  /** Encode 4096 texels back to a 2048-byte 4-bit map (texel >> 4). */
  inline void encode_alpha_4bit(const std::vector<std::uint8_t>& map, std::vector<std::byte>& out)
  {
    for (std::size_t i = 0; i < alpha_4bit_bytes; ++i)
    {
      const std::uint8_t lo = static_cast<std::uint8_t>(map[2 * i] >> 4);
      const std::uint8_t hi = static_cast<std::uint8_t>(map[2 * i + 1] >> 4);
      out.push_back(static_cast<std::byte>(lo | (hi << 4)));
    }
  }

  /** Encode 4096 texels back to a 4096-byte 8-bit map (a straight copy). */
  inline void encode_alpha_8bit(const std::vector<std::uint8_t>& map, std::vector<std::byte>& out)
  {
    for (std::uint8_t v : map)
      out.push_back(static_cast<std::byte>(v));
  }

  /** RLE-encode 4096 texels, one row at a time (values cannot span rows). A
      greedy encoder: fill runs of >= 3 equal bytes, copy otherwise. This does
      NOT reproduce Blizzard's exact byte stream (their encoder differs), which
      is why ADT round-trip is semantic, not byte-identical. */
  inline void encode_alpha_rle(const std::vector<std::uint8_t>& map, std::vector<std::byte>& out)
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

  /** Decode a 512-byte 1-bit shadow bitmap to 4096 texels (0/1), LSB first. */
  inline void decode_shadow(std::span<const std::byte> src, std::vector<std::uint8_t>& out)
  {
    out.assign(alpha_texels, 0);
    for (std::size_t i = 0; i < alpha_texels && i / 8 < src.size(); ++i)
    {
      const auto b = std::to_integer<std::uint8_t>(src[i / 8]);
      out[i] = (b >> (i % 8)) & 1;
    }
  }

  /** Encode 4096 texels (0/1) back to a 512-byte 1-bit bitmap, LSB first. */
  inline void encode_shadow(const std::vector<std::uint8_t>& map, std::vector<std::byte>& out)
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
}
