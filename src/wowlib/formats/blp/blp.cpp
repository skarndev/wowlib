/** @file
    Definitions for the BLP entity and its pixel codecs. This is the only TU
    that sees stb_dxt (the DXT/BC compressor); decoding is wowlib's own. */

#include <wowlib/formats/blp/blp.hpp>
#include <wowlib/formats/blp/codec.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>

#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

namespace wowlib::formats::blp::detail
{
  namespace
  {
    /** Expand a 5-bit channel to 8 bits by bit replication. */
    constexpr std::uint8_t expand5(std::uint32_t v) noexcept
    {
      return static_cast<std::uint8_t>((v << 3) | (v >> 2));
    }

    /** Expand a 6-bit channel to 8 bits by bit replication. */
    constexpr std::uint8_t expand6(std::uint32_t v) noexcept
    {
      return static_cast<std::uint8_t>((v << 2) | (v >> 4));
    }

    /** Read a little-endian u16 from raw block bytes. */
    std::uint16_t read_u16(const std::uint8_t* at) noexcept
    {
      return static_cast<std::uint16_t>(at[0] | (at[1] << 8));
    }

    /** Scatter one decoded 4x4 block into a level image, clipping the
        right/bottom partial-block texels. */
    void place_block(const BlockPixels& block, std::span<std::uint8_t> rgba, std::uint32_t width,
                     std::uint32_t height, std::uint32_t bx, std::uint32_t by) noexcept
    {
      for (std::uint32_t py = 0; py < 4; ++py)
      {
        const std::uint32_t y = by * 4 + py;
        if (y >= height)
          break;
        for (std::uint32_t px = 0; px < 4; ++px)
        {
          const std::uint32_t x = bx * 4 + px;
          if (x >= width)
            break;
          const std::size_t src = (py * 4 + px) * std::size_t{4};
          const std::size_t dst = (std::size_t{y} * width + x) * 4;
          std::copy_n(block.data() + src, 4, rgba.data() + dst);
        }
      }
    }
  }

  // --- DxtCodec ---------------------------------------------------------------

  void DxtCodec::decode_color_block(const std::uint8_t* block, BlockPixels& out,
                                    bool punch_through) noexcept
  {
    const std::uint16_t c0 = read_u16(block);
    const std::uint16_t c1 = read_u16(block + 2);

    std::array<std::array<std::uint8_t, 4>, 4> colors{};
    const auto expand = [](std::uint16_t c) -> std::array<std::uint8_t, 4> {
      return {expand5((c >> 11) & 0x1F), expand6((c >> 5) & 0x3F), expand5(c & 0x1F), 255};
    };
    colors[0] = expand(c0);
    colors[1] = expand(c1);

    if (!punch_through || c0 > c1)
    {
      for (int ch = 0; ch < 3; ++ch)
      {
        colors[2][static_cast<std::size_t>(ch)] = static_cast<std::uint8_t>(
          (2 * colors[0][static_cast<std::size_t>(ch)] + colors[1][static_cast<std::size_t>(ch)])
          / 3);
        colors[3][static_cast<std::size_t>(ch)] = static_cast<std::uint8_t>(
          (colors[0][static_cast<std::size_t>(ch)] + 2 * colors[1][static_cast<std::size_t>(ch)])
          / 3);
      }
      colors[2][3] = 255;
      colors[3][3] = 255;
    }
    else
    {
      for (int ch = 0; ch < 3; ++ch)
        colors[2][static_cast<std::size_t>(ch)] = static_cast<std::uint8_t>(
          (colors[0][static_cast<std::size_t>(ch)] + colors[1][static_cast<std::size_t>(ch)]) / 2);
      colors[2][3] = 255;
      colors[3] = {0, 0, 0, 0};  // the punch-through texel
    }

    for (std::uint32_t texel = 0; texel < 16; ++texel)
    {
      const std::uint32_t bits = block[4 + texel / 4];
      const std::uint32_t index = (bits >> (2 * (texel % 4))) & 0x3;
      std::copy_n(colors[index].data(), 4, out.data() + texel * std::size_t{4});
    }
  }

  void DxtCodec::decode_explicit_alpha(const std::uint8_t* block, BlockPixels& out) noexcept
  {
    for (std::uint32_t texel = 0; texel < 16; ++texel)
    {
      const std::uint32_t nibble = (block[texel / 2] >> (4 * (texel % 2))) & 0xF;
      out[texel * 4 + 3] = static_cast<std::uint8_t>(nibble * 0x11);
    }
  }

  void DxtCodec::decode_interpolated_alpha(const std::uint8_t* block,
                                           std::span<std::uint8_t, 16> out) noexcept
  {
    const std::uint8_t a0 = block[0];
    const std::uint8_t a1 = block[1];
    std::array<std::uint8_t, 8> values{a0, a1};
    if (a0 > a1)
      for (int i = 1; i <= 6; ++i)
        values[static_cast<std::size_t>(i) + 1] =
          static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
    else
    {
      for (int i = 1; i <= 4; ++i)
        values[static_cast<std::size_t>(i) + 1] =
          static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
      values[6] = 0;
      values[7] = 255;
    }

    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i)
      bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
    for (std::uint32_t texel = 0; texel < 16; ++texel)
      out[texel] = values[(bits >> (3 * texel)) & 0x7];
  }

  BlockPixels DxtCodec::extract_block(std::span<const std::uint8_t> rgba, std::uint32_t width,
                                      std::uint32_t height, std::uint32_t bx,
                                      std::uint32_t by) noexcept
  {
    BlockPixels out{};
    for (std::uint32_t py = 0; py < 4; ++py)
    {
      const std::uint32_t y = std::min(by * 4 + py, height - 1);
      for (std::uint32_t px = 0; px < 4; ++px)
      {
        const std::uint32_t x = std::min(bx * 4 + px, width - 1);
        const std::size_t src = (std::size_t{y} * width + x) * 4;
        std::copy_n(rgba.data() + src, 4, out.data() + (py * 4 + px) * std::size_t{4});
      }
    }
    return out;
  }

  void DxtCodec::encode_punch_through_block(const BlockPixels& pixels, std::uint8_t* dest) noexcept
  {
    // bounding-box endpoints over the opaque texels
    std::uint8_t lo[3] = {255, 255, 255};
    std::uint8_t hi[3] = {0, 0, 0};
    bool any_opaque = false;
    for (std::uint32_t texel = 0; texel < 16; ++texel)
    {
      if (pixels[texel * 4 + 3] < 128)
        continue;
      any_opaque = true;
      for (int ch = 0; ch < 3; ++ch)
      {
        lo[ch] = std::min(lo[ch], pixels[texel * 4 + static_cast<std::size_t>(ch)]);
        hi[ch] = std::max(hi[ch], pixels[texel * 4 + static_cast<std::size_t>(ch)]);
      }
    }
    if (!any_opaque)
    {
      lo[0] = lo[1] = lo[2] = 0;
      hi[0] = hi[1] = hi[2] = 0;
    }

    const auto pack565 = [](const std::uint8_t* c) -> std::uint16_t {
      return static_cast<std::uint16_t>(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
    };
    std::uint16_t c0 = pack565(lo);
    std::uint16_t c1 = pack565(hi);
    if (c0 > c1)
      std::swap(c0, c1);  // color0 <= color1 selects the 3-color + transparent mode

    const std::array<std::array<std::int32_t, 3>, 3> palette = {{
      {expand5((c0 >> 11) & 0x1F), expand6((c0 >> 5) & 0x3F), expand5(c0 & 0x1F)},
      {expand5((c1 >> 11) & 0x1F), expand6((c1 >> 5) & 0x3F), expand5(c1 & 0x1F)},
      {(expand5((c0 >> 11) & 0x1F) + expand5((c1 >> 11) & 0x1F)) / 2,
       (expand6((c0 >> 5) & 0x3F) + expand6((c1 >> 5) & 0x3F)) / 2,
       (expand5(c0 & 0x1F) + expand5(c1 & 0x1F)) / 2},
    }};

    dest[0] = static_cast<std::uint8_t>(c0 & 0xFF);
    dest[1] = static_cast<std::uint8_t>(c0 >> 8);
    dest[2] = static_cast<std::uint8_t>(c1 & 0xFF);
    dest[3] = static_cast<std::uint8_t>(c1 >> 8);
    for (std::uint32_t row = 0; row < 4; ++row)
    {
      std::uint8_t bits = 0;
      for (std::uint32_t col = 0; col < 4; ++col)
      {
        const std::uint32_t texel = row * 4 + col;
        std::uint32_t index = 3;  // transparent
        if (pixels[texel * 4 + 3] >= 128)
        {
          std::int64_t best = -1;
          for (std::uint32_t candidate = 0; candidate < 3; ++candidate)
          {
            std::int64_t dist = 0;
            for (int ch = 0; ch < 3; ++ch)
            {
              const std::int64_t d = palette[candidate][static_cast<std::size_t>(ch)]
                                     - pixels[texel * 4 + static_cast<std::size_t>(ch)];
              dist += d * d;
            }
            if (best < 0 || dist < best)
            {
              best = dist;
              index = candidate;
            }
          }
        }
        bits |= static_cast<std::uint8_t>(index << (2 * col));
      }
      dest[4 + row] = bits;
    }
  }

  Result<std::vector<std::uint8_t>> DxtCodec::decode(std::span<const std::byte> src,
                                                     std::uint32_t width,
                                                     std::uint32_t height) const
  {
    const std::uint32_t blocks_x = (width + 3) / 4;
    const std::uint32_t blocks_y = (height + 3) / 4;
    std::vector<std::uint8_t> rgba(std::size_t{width} * height * 4, 0);

    const std::size_t stride = block_bytes();
    std::size_t at = 0;
    for (std::uint32_t by = 0; by < blocks_y; ++by)
      for (std::uint32_t bx = 0; bx < blocks_x; ++bx, at += stride)
      {
        if (at + stride > src.size())
          return rgba;  // understated mip size (known client quirk): pad transparent black

        // std::byte -> the byte values the block decoders read
        std::array<std::uint8_t, 16> raw{};
        std::memcpy(raw.data(), src.data() + at, stride);

        BlockPixels block{};
        switch (format_)
        {
          case PixelFormat::Dxt1:
            decode_color_block(raw.data(), block, true);
            break;
          case PixelFormat::Dxt3:
            decode_color_block(raw.data() + 8, block, false);
            decode_explicit_alpha(raw.data(), block);
            break;
          case PixelFormat::Dxt5:
          {
            decode_color_block(raw.data() + 8, block, false);
            std::array<std::uint8_t, 16> alpha{};
            decode_interpolated_alpha(raw.data(), alpha);
            for (std::uint32_t texel = 0; texel < 16; ++texel)
              block[texel * 4 + 3] = alpha[texel];
            break;
          }
          case PixelFormat::Bc5:
          {
            std::array<std::uint8_t, 16> red{};
            std::array<std::uint8_t, 16> green{};
            decode_interpolated_alpha(raw.data(), red);
            decode_interpolated_alpha(raw.data() + 8, green);
            for (std::uint32_t texel = 0; texel < 16; ++texel)
            {
              block[texel * 4 + 0] = red[texel];
              block[texel * 4 + 1] = green[texel];
              block[texel * 4 + 2] = 0;
              block[texel * 4 + 3] = 255;
            }
            break;
          }
          default:
            return make_error(ErrorCode::NotSupported,
                              std::format("BLP DXT decode: pixel format {} is not a block format",
                                          std::to_underlying(format_)));
        }
        place_block(block, rgba, width, height, bx, by);
      }
    return rgba;
  }

  Result<FileBuffer> DxtCodec::encode(std::span<const std::uint8_t> rgba, std::uint32_t width,
                                      std::uint32_t height) const
  {
    const std::uint32_t blocks_x = (width + 3) / 4;
    const std::uint32_t blocks_y = (height + 3) / 4;
    const std::size_t stride = block_bytes();
    FileBuffer out(std::size_t{blocks_x} * blocks_y * stride);

    std::size_t at = 0;
    for (std::uint32_t by = 0; by < blocks_y; ++by)
      for (std::uint32_t bx = 0; bx < blocks_x; ++bx, at += stride)
      {
        const BlockPixels block = extract_block(rgba, width, height, bx, by);
        std::array<std::uint8_t, 16> encoded{};
        switch (format_)
        {
          case PixelFormat::Dxt1:
          {
            const bool transparent =
              std::ranges::any_of(std::views::iota(0u, 16u),
                                  [&](std::uint32_t texel) { return block[texel * 4 + 3] < 128; });
            if (transparent)
              encode_punch_through_block(block, encoded.data());
            else
              stb_compress_dxt_block(encoded.data(), block.data(), 0, STB_DXT_HIGHQUAL);
            break;
          }
          case PixelFormat::Dxt3:
          {
            // explicit 4-bit alpha block, then the (always 4-color) color block
            for (std::uint32_t texel = 0; texel < 16; ++texel)
            {
              const std::uint32_t nibble =
                std::min<std::uint32_t>(15, (block[texel * 4 + 3] + std::uint32_t{8}) / 17);
              encoded[texel / 2] |= static_cast<std::uint8_t>(nibble << (4 * (texel % 2)));
            }
            stb_compress_dxt_block(encoded.data() + 8, block.data(), 0, STB_DXT_HIGHQUAL);
            break;
          }
          case PixelFormat::Dxt5:
            stb_compress_dxt_block(encoded.data(), block.data(), 1, STB_DXT_HIGHQUAL);
            break;
          case PixelFormat::Bc5:
          {
            std::array<std::uint8_t, 16 * 2> rg{};
            for (std::uint32_t texel = 0; texel < 16; ++texel)
            {
              rg[texel * 2 + 0] = block[texel * 4 + 0];
              rg[texel * 2 + 1] = block[texel * 4 + 1];
            }
            stb_compress_bc5_block(encoded.data(), rg.data());
            break;
          }
          default:
            return make_error(ErrorCode::NotSupported,
                              std::format("BLP DXT encode: pixel format {} is not a block format",
                                          std::to_underlying(format_)));
        }
        std::memcpy(out.data() + at, encoded.data(), stride);
      }
    return out;
  }

  // --- PaletteCodec -----------------------------------------------------------

  std::uint8_t PaletteCodec::unpack_alpha(std::span<const std::byte> plane,
                                          std::size_t index) const noexcept
  {
    switch (alpha_depth_)
    {
      case 1:
      {
        if (index / 8 >= plane.size())
          return 0;
        const auto bits = std::to_integer<std::uint8_t>(plane[index / 8]);
        return ((bits >> (index % 8)) & 1) ? 255 : 0;
      }
      case 4:
      {
        if (index / 2 >= plane.size())
          return 0;
        const auto bits = std::to_integer<std::uint8_t>(plane[index / 2]);
        const std::uint8_t nibble = (bits >> (4 * (index % 2))) & 0xF;
        return static_cast<std::uint8_t>(nibble * 0x11);
      }
      case 8:
        return index < plane.size() ? std::to_integer<std::uint8_t>(plane[index]) : 0;
      default:
        return 255;  // depth 0: fully opaque
    }
  }

  void PaletteCodec::pack_alpha(std::uint8_t alpha, std::span<std::byte> plane,
                                std::size_t index) const noexcept
  {
    switch (alpha_depth_)
    {
      case 1:
        if (alpha >= 128)
          plane[index / 8] |= static_cast<std::byte>(1 << (index % 8));
        break;
      case 4:
      {
        const std::uint32_t nibble = std::min<std::uint32_t>(15, (alpha + std::uint32_t{8}) / 17);
        plane[index / 2] |= static_cast<std::byte>(nibble << (4 * (index % 2)));
        break;
      }
      case 8:
        plane[index] = static_cast<std::byte>(alpha);
        break;
      default:
        break;  // depth 0: no plane
    }
  }

  std::uint8_t PaletteCodec::nearest_index(std::span<const CImVector, blp_palette_size> palette,
                                           std::uint8_t r, std::uint8_t g,
                                           std::uint8_t b) noexcept
  {
    std::uint32_t best_index = 0;
    std::int64_t best = -1;
    for (std::uint32_t i = 0; i < blp_palette_size; ++i)
    {
      const std::int64_t dr = std::int64_t{palette[i].r} - r;
      const std::int64_t dg = std::int64_t{palette[i].g} - g;
      const std::int64_t db = std::int64_t{palette[i].b} - b;
      const std::int64_t dist = dr * dr + dg * dg + db * db;
      if (best < 0 || dist < best)
      {
        best = dist;
        best_index = i;
      }
    }
    return static_cast<std::uint8_t>(best_index);
  }

  Result<std::vector<std::uint8_t>> PaletteCodec::decode(
    std::span<const std::byte> src, std::span<const CImVector, blp_palette_size> palette,
    std::uint32_t width, std::uint32_t height) const
  {
    const std::size_t texels = std::size_t{width} * height;
    std::vector<std::uint8_t> rgba(texels * 4, 0);
    const std::span<const std::byte> plane =
      src.size() > texels ? src.subspan(texels) : std::span<const std::byte>{};

    for (std::size_t i = 0; i < texels && i < src.size(); ++i)
    {
      const CImVector& color = palette[std::to_integer<std::uint8_t>(src[i])];
      rgba[i * 4 + 0] = color.r;
      rgba[i * 4 + 1] = color.g;
      rgba[i * 4 + 2] = color.b;
      rgba[i * 4 + 3] = unpack_alpha(plane, i);
    }
    return rgba;
  }

  Result<FileBuffer> PaletteCodec::encode(std::span<const std::uint8_t> rgba,
                                          std::span<const CImVector, blp_palette_size> palette,
                                          std::uint32_t width, std::uint32_t height) const
  {
    const std::size_t texels = std::size_t{width} * height;
    FileBuffer out(encoded_size(width, height));
    const std::span<std::byte> plane = std::span{out}.subspan(texels);

    // memoize distinct colors: nearest_index is a 256-entry scan, and images
    // repeat colors heavily
    std::unordered_map<std::uint32_t, std::uint8_t> memo;
    for (std::size_t i = 0; i < texels; ++i)
    {
      const std::uint8_t r = rgba[i * 4 + 0];
      const std::uint8_t g = rgba[i * 4 + 1];
      const std::uint8_t b = rgba[i * 4 + 2];
      const std::uint32_t key = (std::uint32_t{r} << 16) | (std::uint32_t{g} << 8) | b;
      auto found = memo.find(key);
      if (found == memo.end())
        found = memo.emplace(key, nearest_index(palette, r, g, b)).first;
      out[i] = static_cast<std::byte>(found->second);
      pack_alpha(rgba[i * 4 + 3], plane, i);
    }
    return out;
  }

  std::array<CImVector, blp_palette_size> PaletteCodec::build_palette(
    std::span<const std::uint8_t> rgba) const
  {
    // collect distinct opaque RGB values with occurrence counts
    std::unordered_map<std::uint32_t, std::uint32_t> counts;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
    {
      const std::uint32_t key = (std::uint32_t{rgba[i]} << 16) | (std::uint32_t{rgba[i + 1]} << 8)
                                | rgba[i + 2];
      ++counts[key];
    }

    std::array<CImVector, blp_palette_size> palette{};
    const auto entry = [](std::uint32_t key) {
      return CImVector{.b = static_cast<std::uint8_t>(key & 0xFF),
                       .g = static_cast<std::uint8_t>((key >> 8) & 0xFF),
                       .r = static_cast<std::uint8_t>((key >> 16) & 0xFF),
                       .a = 0};
    };

    if (counts.size() <= blp_palette_size)
    {
      std::size_t at = 0;
      for (const auto& [key, count] : counts)
        palette[at++] = entry(key);
      return palette;
    }

    // median cut: split the box with the longest axis at its median until 256
    struct Point
    {
      std::uint8_t r, g, b;
      std::uint32_t count;
    };
    std::vector<Point> points;
    points.reserve(counts.size());
    for (const auto& [key, count] : counts)
      points.push_back({static_cast<std::uint8_t>((key >> 16) & 0xFF),
                        static_cast<std::uint8_t>((key >> 8) & 0xFF),
                        static_cast<std::uint8_t>(key & 0xFF), count});

    struct Box
    {
      std::size_t begin, end;  // the points range
    };
    std::vector<Box> boxes{{0, points.size()}};
    const auto channel = [](const Point& p, int ch) {
      return ch == 0 ? p.r : ch == 1 ? p.g : p.b;
    };

    while (boxes.size() < blp_palette_size)
    {
      // the box with the longest color axis; stop when nothing is splittable
      std::size_t pick = boxes.size();
      int pick_axis = 0;
      int pick_extent = -1;
      for (std::size_t i = 0; i < boxes.size(); ++i)
      {
        if (boxes[i].end - boxes[i].begin < 2)
          continue;
        for (int axis = 0; axis < 3; ++axis)
        {
          int lo = 256;
          int hi = -1;
          for (std::size_t p = boxes[i].begin; p < boxes[i].end; ++p)
          {
            lo = std::min<int>(lo, channel(points[p], axis));
            hi = std::max<int>(hi, channel(points[p], axis));
          }
          if (hi - lo > pick_extent)
          {
            pick_extent = hi - lo;
            pick = i;
            pick_axis = axis;
          }
        }
      }
      if (pick == boxes.size() || pick_extent <= 0)
        break;

      Box& box = boxes[pick];
      const auto mid =
        points.begin() + static_cast<std::ptrdiff_t>(box.begin + (box.end - box.begin) / 2);
      std::nth_element(points.begin() + static_cast<std::ptrdiff_t>(box.begin), mid,
                       points.begin() + static_cast<std::ptrdiff_t>(box.end),
                       [&](const Point& a, const Point& other) {
                         return channel(a, pick_axis) < channel(other, pick_axis);
                       });
      const std::size_t split = box.begin + (box.end - box.begin) / 2;
      const std::size_t old_end = box.end;
      box.end = split;
      boxes.push_back({split, old_end});
    }

    // each box contributes its count-weighted average color
    for (std::size_t i = 0; i < boxes.size(); ++i)
    {
      std::uint64_t r = 0;
      std::uint64_t g = 0;
      std::uint64_t b = 0;
      std::uint64_t total = 0;
      for (std::size_t p = boxes[i].begin; p < boxes[i].end; ++p)
      {
        r += std::uint64_t{points[p].r} * points[p].count;
        g += std::uint64_t{points[p].g} * points[p].count;
        b += std::uint64_t{points[p].b} * points[p].count;
        total += points[p].count;
      }
      if (total == 0)
        continue;
      palette[i] = CImVector{.b = static_cast<std::uint8_t>(b / total),
                             .g = static_cast<std::uint8_t>(g / total),
                             .r = static_cast<std::uint8_t>(r / total),
                             .a = 0};
    }
    return palette;
  }

  // --- RawCodec ---------------------------------------------------------------

  Result<std::vector<std::uint8_t>> RawCodec::decode(std::span<const std::byte> src,
                                                     std::uint32_t width,
                                                     std::uint32_t height) const
  {
    const std::size_t texels = std::size_t{width} * height;
    std::vector<std::uint8_t> rgba(texels * 4, 0);
    for (std::size_t i = 0; i < texels && i * 4 + 3 < src.size(); ++i)
    {
      rgba[i * 4 + 0] = std::to_integer<std::uint8_t>(src[i * 4 + 2]);
      rgba[i * 4 + 1] = std::to_integer<std::uint8_t>(src[i * 4 + 1]);
      rgba[i * 4 + 2] = std::to_integer<std::uint8_t>(src[i * 4 + 0]);
      rgba[i * 4 + 3] = std::to_integer<std::uint8_t>(src[i * 4 + 3]);
    }
    return rgba;
  }

  Result<FileBuffer> RawCodec::encode(std::span<const std::uint8_t> rgba, std::uint32_t width,
                                      std::uint32_t height) const
  {
    const std::size_t texels = std::size_t{width} * height;
    FileBuffer out(texels * 4);
    for (std::size_t i = 0; i < texels; ++i)
    {
      out[i * 4 + 0] = static_cast<std::byte>(rgba[i * 4 + 2]);
      out[i * 4 + 1] = static_cast<std::byte>(rgba[i * 4 + 1]);
      out[i * 4 + 2] = static_cast<std::byte>(rgba[i * 4 + 0]);
      out[i * 4 + 3] = static_cast<std::byte>(rgba[i * 4 + 3]);
    }
    return out;
  }

  // --- MipmapScaler -----------------------------------------------------------

  std::vector<std::uint8_t> MipmapScaler::downscale(std::span<const std::uint8_t> rgba,
                                                    std::uint32_t width,
                                                    std::uint32_t height) const
  {
    const std::uint32_t out_w = std::max<std::uint32_t>(1, width / 2);
    const std::uint32_t out_h = std::max<std::uint32_t>(1, height / 2);
    std::vector<std::uint8_t> out(std::size_t{out_w} * out_h * 4);

    for (std::uint32_t y = 0; y < out_h; ++y)
      for (std::uint32_t x = 0; x < out_w; ++x)
      {
        const std::uint32_t x0 = std::min(2 * x, width - 1);
        const std::uint32_t x1 = std::min(2 * x + 1, width - 1);
        const std::uint32_t y0 = std::min(2 * y, height - 1);
        const std::uint32_t y1 = std::min(2 * y + 1, height - 1);
        for (std::uint32_t ch = 0; ch < 4; ++ch)
        {
          const int sum = rgba[(std::size_t{y0} * width + x0) * 4 + ch]
                                    + rgba[(std::size_t{y0} * width + x1) * 4 + ch]
                                    + rgba[(std::size_t{y1} * width + x0) * 4 + ch]
                                    + rgba[(std::size_t{y1} * width + x1) * 4 + ch];
          out[(std::size_t{y} * out_w + x) * 4 + ch] = static_cast<std::uint8_t>((sum + 2) / 4);
        }
      }
    return out;
  }
}

namespace wowlib::formats::blp
{
  namespace
  {
    /** The resolved DXT block format of a file: the preferred_format when it
        names one, else the classic alpha-depth heuristic every reader uses. */
    PixelFormat resolve_dxt_format(PixelFormat preferred, std::uint8_t alpha_depth) noexcept
    {
      switch (preferred)
      {
        case PixelFormat::Dxt1:
        case PixelFormat::Dxt3:
        case PixelFormat::Dxt5:
        case PixelFormat::Bc5:
          return preferred;
        default:
          return alpha_depth <= 1 ? PixelFormat::Dxt1
                                  : alpha_depth == 4 ? PixelFormat::Dxt3 : PixelFormat::Dxt5;
      }
    }
  }

  Result<void> BLP::read(std::span<const std::byte> data)
  {
    if (data.size() < blp_header_bytes)
      return make_error(ErrorCode::ChunkTruncated,
                        std::format("BLP file is {} bytes; the header region is {}", data.size(),
                                    blp_header_bytes));

    detail::BLPHeader header{};
    std::memcpy(&header, data.data(), sizeof header);
    if (header.magic != blp_magic)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("not a BLP2 file (magic {:#010x})", header.magic));
    if (header.color_encoding > std::to_underlying(ColorEncoding::BgraAlt))
      return make_error(ErrorCode::NotSupported,
                        std::format("BLP colorEncoding {} is unknown", header.color_encoding));
    if (header.preferred_format > std::to_underlying(PixelFormat::Bc5)
        || header.preferred_format == 10)
      return make_error(ErrorCode::NotSupported,
                        std::format("BLP preferredFormat {} is unknown", header.preferred_format));

    *this = BLP{};
    version = header.version;
    color_encoding = static_cast<ColorEncoding>(header.color_encoding);
    alpha_depth = header.alpha_depth;
    preferred_format = static_cast<PixelFormat>(header.preferred_format);
    mip_flags = header.mip_flags;
    width = header.width;
    height = header.height;
    std::memcpy(palette.data(), data.data() + sizeof header, sizeof palette);

    mips.clear();
    for (std::size_t level = 0; level < blp_max_mips; ++level)
    {
      const std::uint32_t offset = header.mip_offsets[level];
      const std::uint32_t size = header.mip_sizes[level];
      if (offset == 0 || size == 0)
        continue;
      if (std::uint64_t{offset} + size > data.size())
        return make_error(ErrorCode::OffsetOutOfBounds,
                          std::format("BLP mip {} spans [{}, {}) beyond the {}-byte file", level,
                                      offset, std::uint64_t{offset} + size, data.size()));
      mips.resize(std::max(mips.size(), level + 1));
      mips[level].assign(data.begin() + offset, data.begin() + offset + size);
    }

    // record the on-disk placement + every byte no region claims, so write()
    // can replay unusual layouts byte-perfectly
    stored_layout.offsets = header.mip_offsets;
    stored_layout.sizes = header.mip_sizes;
    stored_layout.file_size = static_cast<std::uint32_t>(data.size());
    std::vector<std::pair<std::uint64_t, std::uint64_t>> covered{{0, blp_header_bytes}};
    for (std::size_t level = 0; level < blp_max_mips; ++level)
      if (header.mip_offsets[level] != 0 && header.mip_sizes[level] != 0)
        covered.emplace_back(header.mip_offsets[level],
                             std::uint64_t{header.mip_offsets[level]} + header.mip_sizes[level]);
    std::ranges::sort(covered);
    stored_layout.gaps.clear();
    std::uint64_t reached = 0;
    for (const auto& [begin, end] : covered)
    {
      if (begin > reached)
        stored_layout.gaps.push_back(
          {static_cast<std::uint32_t>(reached),
           FileBuffer{data.begin() + static_cast<std::ptrdiff_t>(reached),
                      data.begin() + static_cast<std::ptrdiff_t>(begin)}});
      reached = std::max(reached, end);
    }
    if (reached < data.size())
      stored_layout.gaps.push_back({static_cast<std::uint32_t>(reached),
                                    FileBuffer{data.begin() + static_cast<std::ptrdiff_t>(reached),
                                               data.end()}});
    stored_layout.engaged = true;
    return {};
  }

  Result<FileBuffer> BLP::write() const
  {
    detail::BLPHeader header{};
    header.magic = blp_magic;
    header.version = version;
    header.color_encoding = std::to_underlying(color_encoding);
    header.alpha_depth = alpha_depth;
    header.preferred_format = std::to_underlying(preferred_format);
    header.mip_flags = mip_flags;
    header.width = width;
    header.height = height;

    // replay the recorded layout while every payload still matches its
    // recorded size; otherwise lay the file out canonically
    bool replay = stored_layout.engaged;
    if (replay)
      for (std::size_t level = 0; level < blp_max_mips; ++level)
      {
        const bool stored = stored_layout.offsets[level] != 0 && stored_layout.sizes[level] != 0;
        const bool present = level < mips.size() && !mips[level].empty();
        if (stored != present
            || (stored && mips[level].size() != stored_layout.sizes[level]))
        {
          replay = false;
          break;
        }
      }

    if (replay)
    {
      header.mip_offsets = stored_layout.offsets;
      header.mip_sizes = stored_layout.sizes;
      FileBuffer out(stored_layout.file_size);
      std::memcpy(out.data(), &header, sizeof header);
      std::memcpy(out.data() + sizeof header, palette.data(), sizeof palette);
      for (std::size_t level = 0; level < blp_max_mips; ++level)
        if (header.mip_offsets[level] != 0 && header.mip_sizes[level] != 0)
          std::memcpy(out.data() + header.mip_offsets[level], mips[level].data(),
                      mips[level].size());
      for (const detail::StoredLayout::Run& gap : stored_layout.gaps)
        std::memcpy(out.data() + gap.offset, gap.bytes.data(), gap.bytes.size());
      return out;
    }

    if (mips.size() > blp_max_mips)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("a BLP addresses at most {} mip levels, {} stored",
                                    blp_max_mips, mips.size()));

    std::uint64_t cursor = blp_header_bytes;
    for (std::size_t level = 0; level < mips.size(); ++level)
    {
      if (mips[level].empty())
        continue;
      header.mip_offsets[level] = static_cast<std::uint32_t>(cursor);
      header.mip_sizes[level] = static_cast<std::uint32_t>(mips[level].size());
      cursor += mips[level].size();
    }
    if (cursor > std::numeric_limits<std::uint32_t>::max())
      return make_error(ErrorCode::InvalidEntityState,
                        "BLP mip payloads exceed the u32 offset space");

    FileBuffer out(cursor);
    std::memcpy(out.data(), &header, sizeof header);
    std::memcpy(out.data() + sizeof header, palette.data(), sizeof palette);
    for (std::size_t level = 0; level < mips.size(); ++level)
      if (!mips[level].empty())
        std::memcpy(out.data() + header.mip_offsets[level], mips[level].data(),
                    mips[level].size());
    return out;
  }

  Result<void> BLP::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto data = fs.read_file(key);
    if (!data)
      return std::unexpected{data.error()};
    return read(*data);
  }

  Result<void> BLP::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "saving a BLP needs a path for the file key");
    return write().and_then(
      [&](FileBuffer data) { return fs.add_file(*resolved.path, data).transform([](auto&&) {}); });
  }

  Result<Image> BLP::decode() const { return decode(0); }

  Result<Image> BLP::decode(std::uint32_t level) const
  {
    if (level >= mips.size() || mips[level].empty())
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("BLP mip level {} is not stored ({} levels present)", level,
                                    mips.size()));
    if (width == 0 || height == 0)
      return make_error(ErrorCode::InvalidEntityState, "BLP has zero width or height");

    const std::uint32_t w = mip_width(level);
    const std::uint32_t h = mip_height(level);
    const std::span<const std::byte> src{mips[level]};

    Result<std::vector<std::uint8_t>> pixels = [&]() -> Result<std::vector<std::uint8_t>> {
      switch (color_encoding)
      {
        case ColorEncoding::Palettized:
          return detail::PaletteCodec{alpha_depth}.decode(src, palette, w, h);
        case ColorEncoding::Dxt:
          return detail::DxtCodec{resolve_dxt_format(preferred_format, alpha_depth)}.decode(src, w,
                                                                                            h);
        case ColorEncoding::Bgra:
        case ColorEncoding::BgraAlt:
          return detail::RawCodec{}.decode(src, w, h);
        case ColorEncoding::Jpeg:
          return make_error(ErrorCode::NotSupported,
                            "JPEG-encoded BLPs never shipped in WoW clients and are not decodable "
                            "(the payload round-trips verbatim)");
      }
      return make_error(ErrorCode::NotSupported, "unreachable: validated on read");
    }();
    if (!pixels)
      return std::unexpected{pixels.error()};
    return Image{.width = w, .height = h, .pixels = std::move(*pixels)};
  }

  Result<void> BLP::encode(const Image& image) { return encode(image, EncodeSettings{}); }

  Result<void> BLP::encode(const Image& image, const EncodeSettings& settings)
  {
    if (image.width == 0 || image.height == 0)
      return make_error(ErrorCode::InvalidEntityState, "encoding needs a non-empty image");
    if (image.pixels.size() != std::size_t{image.width} * image.height * 4)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("image holds {} pixel bytes; {}x{} RGBA needs {}",
                                    image.pixels.size(), image.width, image.height,
                                    std::size_t{image.width} * image.height * 4));
    if (settings.encoding == ColorEncoding::Jpeg)
      return make_error(ErrorCode::NotSupported, "wowlib does not produce JPEG-encoded BLPs");
    if (settings.alpha_depth != 0 && settings.alpha_depth != 1 && settings.alpha_depth != 4
        && settings.alpha_depth != 8)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("alpha_depth must be 0, 1, 4 or 8, not {}",
                                    settings.alpha_depth));

    *this = BLP{};
    version = blp_version_1;
    color_encoding =
      settings.encoding == ColorEncoding::BgraAlt ? ColorEncoding::Bgra : settings.encoding;
    width = image.width;
    height = image.height;
    mip_flags = settings.mipmaps ? 1 : 0;

    PixelFormat dxt_format = PixelFormat::Unspecified;
    switch (color_encoding)
    {
      case ColorEncoding::Dxt:
        dxt_format = resolve_dxt_format(settings.format, settings.alpha_depth);
        preferred_format = dxt_format;
        alpha_depth = dxt_format == PixelFormat::Dxt1 ? std::min<std::uint8_t>(
                        settings.alpha_depth, 1)
                                                      : settings.alpha_depth;
        break;
      case ColorEncoding::Palettized:
        preferred_format = PixelFormat::Unspecified;
        alpha_depth = settings.alpha_depth;
        palette = detail::PaletteCodec{alpha_depth}.build_palette(image.pixels);
        break;
      default:  // Bgra
        preferred_format = PixelFormat::Argb8888;
        alpha_depth = 8;
        break;
    }

    // level 0, then box-filtered halvings down to 1x1 (16-level table cap)
    std::vector<std::uint8_t> level_pixels = image.pixels;
    std::uint32_t w = width;
    std::uint32_t h = height;
    const detail::MipmapScaler scaler;
    for (std::size_t level = 0; level < blp_max_mips; ++level)
    {
      Result<FileBuffer> payload = [&]() -> Result<FileBuffer> {
        switch (color_encoding)
        {
          case ColorEncoding::Palettized:
            return detail::PaletteCodec{alpha_depth}.encode(level_pixels, palette, w, h);
          case ColorEncoding::Dxt:
            return detail::DxtCodec{dxt_format}.encode(level_pixels, w, h);
          default:
            return detail::RawCodec{}.encode(level_pixels, w, h);
        }
      }();
      if (!payload)
        return std::unexpected{payload.error()};
      mips.push_back(std::move(*payload));

      if (!settings.mipmaps || (w == 1 && h == 1))
        break;
      level_pixels = scaler.downscale(level_pixels, w, h);
      w = std::max<std::uint32_t>(1, w / 2);
      h = std::max<std::uint32_t>(1, h / 2);
    }
    return {};
  }

  Result<FileBuffer> BLP::mip(std::uint32_t level) const
  {
    if (level >= mips.size() || mips[level].empty())
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("BLP mip level {} is not stored ({} levels present)", level,
                                    mips.size()));
    return mips[level];
  }

  Result<void> BLP::set_mip(std::uint32_t level, std::span<const std::byte> data)
  {
    if (level >= blp_max_mips)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("a BLP addresses at most {} mip levels", blp_max_mips));
    if (data.empty())
      return make_error(ErrorCode::InvalidEntityState,
                        "an empty payload cannot be stored (drop trailing levels by resizing "
                        "instead)");
    mips.resize(std::max<std::size_t>(mips.size(), level + 1));
    mips[level].assign(data.begin(), data.end());
    return {};
  }

  ValidationReport BLP::validate() const
  {
    ValidationReport report;

    if (width == 0 || height == 0)
      report.add_error("width", std::format("zero dimension ({}x{}): the client cannot size "
                                            "the texture", width, height));
    // alpha_depth SIZES the separate alpha plane of a palettized file, but is
    // only a block-format hint elsewhere — and shipped files carry junk in it
    // there (3.3.5a Textures/SunGlare.blp: 136 on a DXT texture), so a bad
    // value is only load-bearing when it decides a payload size.
    if (alpha_depth != 0 && alpha_depth != 1 && alpha_depth != 4 && alpha_depth != 8)
    {
      const std::string what =
        std::format("{} is not one of the 0/1/4/8 depths the client reads", alpha_depth);
      if (color_encoding == ColorEncoding::Palettized)
        report.add_error("alpha_depth", what + " and sizes this file's alpha plane");
      else
        report.add_warning("alpha_depth", what + "; ignored for this encoding");
    }
    if (mips.size() > blp_max_mips)
      report.add_error("mips", std::format("{} levels exceed the {} the header can address",
                                           mips.size(), blp_max_mips));
    if (mips.empty() || mips[0].empty())
      report.add_error("mips[0]", "level 0 carries no payload; every texture needs its base level");

    if (color_encoding == ColorEncoding::Jpeg)
    {
      report.add_warning("color_encoding",
                         "JPEG-encoded BLPs never shipped in WoW clients; the payload "
                         "round-trips verbatim but cannot be decoded");
      return report;
    }

    // Each level's payload must cover the pixels its dimensions imply. A SHORT
    // payload is a documented client quirk for the block/raw encodings (the
    // decoders pad the missing tail with transparent black), so it only warns;
    // for palettized data the client would index past the buffer, so it errors.
    for (std::size_t level = 0; level < mips.size() && !report.full(); ++level)
    {
      if (mips[level].empty())
        continue;
      const auto index = static_cast<std::uint32_t>(level);
      const std::uint32_t w = mip_width(index);
      const std::uint32_t h = mip_height(index);
      const std::size_t pixels = std::size_t{w} * h;
      const std::size_t have = mips[level].size();

      std::size_t want = 0;
      switch (color_encoding)
      {
        case ColorEncoding::Palettized:
          want = pixels + pixels * alpha_depth / 8;
          break;
        case ColorEncoding::Dxt:
          want = detail::DxtCodec{resolve_dxt_format(preferred_format, alpha_depth)}
                   .encoded_size(w, h);
          break;
        case ColorEncoding::Bgra:
        case ColorEncoding::BgraAlt:
          want = pixels * 4;
          break;
        case ColorEncoding::Jpeg:
          break;
      }
      if (have >= want)
        continue;
      const std::string path = std::format("mips[{}]", level);
      const std::string what =
        std::format("{} bytes for a {}x{} level, {} needed", have, w, h, want);
      if (color_encoding == ColorEncoding::Palettized)
        report.add_error(path, what + " — the client would read past the payload");
      else
        report.add_warning(path, what + " — short levels decode as transparent black "
                                        "(a known client quirk)");
    }
    return report;
  }
}
