#pragma once

/** @file
    The BLP pixel codecs (namespace wowlib::formats::blp::detail): DxtCodec,
    PaletteCodec, RawCodec and MipmapScaler.

    Each codec moves one mip level between its on-disk payload bytes and the
    library's single in-memory pixel layout — 8-bit RGBA, row-major, top-left
    origin. Following the house rule (and the adt::detail::AlphaMapCodec
    precedent) each is a class with a small decode()/encode() surface and its
    per-format strategies as protected members: the block format / alpha depth
    is the codec's constructor state, the pixels are call parameters.

    Decoding is wowlib's own (BC1/BC2/BC3/BC5 block decoders, palette lookup +
    alpha-plane unpacking, BGRA swizzle). DXT compression rides on stb_dxt
    (BC1 opaque, BC3, BC5) with two in-house additions stb_dxt lacks: BC1
    punch-through blocks (1-bit alpha) and BC2's explicit 4-bit alpha.
    Definitions live in blp.cpp — the only TU that sees stb_dxt. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/blp/blp.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::blp::detail {
  /** One decoded 4x4 texel block: 16 RGBA pixels, row-major. */
  using BlockPixels = std::array<std::uint8_t, 16 * 4>;

  /** The DXT/BC block codec. Constructed with the resolved block format (its
      state — Dxt1, Dxt3, Dxt5 or Bc5); decode()/encode() move a whole level
      between its block stream and RGBA8 pixels. Tolerates the known client
      quirk of understated mip sizes by decoding short streams into
      transparent-black padding. */
  class DxtCodec {
  public:
    /** @param format the resolved block format (Dxt1, Dxt3, Dxt5 or Bc5). */
    explicit DxtCodec(PixelFormat format) noexcept : format_{format} {}

    /** The byte size of one compressed block (8 for BC1, 16 otherwise).
        @return the block byte size. */
    [[nodiscard]]
    std::size_t block_bytes() const noexcept {
      return format_ == PixelFormat::Dxt1 ? 8 : 16;
    }

    /** The exact payload size of a level per the block formula
        ceil(w/4) * ceil(h/4) * block_bytes().
        @param width  the level width in pixels.
        @param height the level height in pixels.
        @return the encoded byte count. */
    [[nodiscard]]
    std::size_t encoded_size(std::uint32_t width, std::uint32_t height) const noexcept {
      return ((width + 3) / 4) * std::size_t{(height + 3) / 4} * block_bytes();
    }

    /** Decode one level's block stream to RGBA8 pixels.
        @param src    the on-disk block bytes; may be shorter than
                      encoded_size() (a known client quirk) — missing blocks
                      decode transparent black.
        @param width  the level width in pixels.
        @param height the level height in pixels.
        @return width * height * 4 RGBA bytes. */
    [[nodiscard]]
    Result<std::vector<std::uint8_t>> decode(std::span<const std::byte> src,
                                             std::uint32_t width,
                                             std::uint32_t height) const;

    /** Encode RGBA8 pixels to one level's block stream, exactly
        encoded_size() bytes.
        @param rgba   width * height * 4 RGBA bytes.
        @param width  the level width in pixels.
        @param height the level height in pixels.
        @return the compressed block bytes. */
    [[nodiscard]]
    Result<FileBuffer> encode(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) const;

  protected:
    /** Decode one BC1 color block (8 bytes) into 16 RGBA pixels.
        @param block       the 8 color-block bytes.
        @param out         the 16 decoded pixels.
        @param punch_through whether the color0 <= color1 3-color mode maps
                            index 3 to transparent black (BC1 semantics; BC2/
                            BC3 color blocks always decode 4-color). */
    static void decode_color_block(const std::uint8_t* block, BlockPixels& out, bool punch_through) noexcept;

    /** Decode one BC2 explicit-alpha block (8 bytes, 4 bits per texel) onto
        the alpha channel of @a out.
        @param block the 8 alpha bytes.
        @param out   the 16 pixels to stamp alpha into. */
    static void decode_explicit_alpha(const std::uint8_t* block, BlockPixels& out) noexcept;

    /** Decode one BC3/BC4 interpolated-alpha block (8 bytes) into 16 values.
        @param block the 8 alpha-block bytes.
        @param out   the 16 decoded 8-bit values, row-major. */
    static void decode_interpolated_alpha(const std::uint8_t* block, std::span<std::uint8_t, 16> out) noexcept;

    /** Encode one BC1 punch-through block (color0 <= color1 3-color mode,
        alpha < 128 texels as index 3) — the mode stb_dxt does not emit.
        @param pixels the 16 RGBA texels.
        @param dest   the 8 output bytes. */
    static void encode_punch_through_block(const BlockPixels& pixels, std::uint8_t* dest) noexcept;

    /** Extract one 4x4 block from a level, clamping reads at the right/bottom
        edges (partial blocks repeat their last row/column, matching what the
        compressors expect).
        @param rgba   the level's RGBA bytes.
        @param width  the level width in pixels.
        @param height the level height in pixels.
        @param bx     the block column.
        @param by     the block row.
        @return the 16 RGBA texels. */
    static BlockPixels extract_block(std::span<const std::uint8_t> rgba,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     std::uint32_t bx,
                                     std::uint32_t by) noexcept;

  private:
    /** The resolved block format. */
    PixelFormat format_;
  };

  /** The palettized codec. Constructed with the alpha-plane bit depth (its
      state — 0, 1, 4 or 8 bits per pixel); decode() looks indices up in the
      256-entry BGRX palette and unpacks the trailing alpha plane, encode()
      maps pixels to nearest palette entries and packs the plane. Palette
      construction (median-cut quantization) is its own verb so one palette
      serves every mip level. */
  class PaletteCodec {
  public:
    /** @param alpha_depth the alpha plane's bits per pixel (0, 1, 4 or 8). */
    explicit PaletteCodec(std::uint8_t alpha_depth) noexcept : alpha_depth_{alpha_depth} {}

    /** The exact payload size of a level: one index byte per pixel plus the
        packed alpha plane.
        @param width  the level width in pixels.
        @param height the level height in pixels.
        @return the encoded byte count. */
    [[nodiscard]]
    std::size_t encoded_size(std::uint32_t width, std::uint32_t height) const noexcept {
      const std::size_t texels = std::size_t{width} * height;
      return texels + (texels * alpha_depth_ + 7) / 8;
    }

    /** Decode one level's index + alpha-plane payload to RGBA8 pixels.
        @param src     the on-disk payload bytes; short payloads decode their
                       missing texels transparent black.
        @param palette the file's 256-entry color table.
        @param width   the level width in pixels.
        @param height  the level height in pixels.
        @return width * height * 4 RGBA bytes. */
    [[nodiscard]]
    Result<std::vector<std::uint8_t>> decode(std::span<const std::byte> src,
                                             std::span<const CImVector, blp_palette_size> palette,
                                             std::uint32_t width,
                                             std::uint32_t height) const;

    /** Encode RGBA8 pixels against an existing palette: nearest-entry indices
        plus the packed alpha plane, exactly encoded_size() bytes.
        @param rgba    width * height * 4 RGBA bytes.
        @param palette the color table to index into.
        @param width   the level width in pixels.
        @param height  the level height in pixels.
        @return the payload bytes. */
    [[nodiscard]]
    Result<FileBuffer> encode(std::span<const std::uint8_t> rgba,
                              std::span<const CImVector, blp_palette_size> palette,
                              std::uint32_t width,
                              std::uint32_t height) const;

    /** Build a 256-entry palette for an image (median-cut over the opaque RGB
        values; an image with <= 256 distinct colors gets them verbatim).
        @param rgba the level-0 RGBA bytes.
        @return the color table (unused tail entries zero). */
    [[nodiscard]]
    std::array<CImVector, blp_palette_size> build_palette(std::span<const std::uint8_t> rgba) const;

  protected:
    /** The alpha value of texel @a index unpacked from the plane at this
        codec's bit depth (255 when the depth is 0).
        @param plane the packed alpha plane bytes.
        @param index the texel ordinal.
        @return the 8-bit alpha value. */
    std::uint8_t unpack_alpha(std::span<const std::byte> plane, std::size_t index) const noexcept;

    /** Pack one texel's alpha into the plane at this codec's bit depth.
        @param alpha the 8-bit alpha value.
        @param plane the packed plane bytes (pre-sized, zero-filled).
        @param index the texel ordinal. */
    void pack_alpha(std::uint8_t alpha, std::span<std::byte> plane, std::size_t index) const noexcept;

    /** The palette entry nearest to (r, g, b) by squared RGB distance.
        @param palette the color table.
        @param r       the red value.
        @param g       the green value.
        @param b       the blue value.
        @return the nearest entry's index. */
    static std::uint8_t nearest_index(std::span<const CImVector, blp_palette_size> palette,
                                      std::uint8_t r,
                                      std::uint8_t g,
                                      std::uint8_t b) noexcept;

  private:
    /** The alpha plane's bits per pixel. */
    std::uint8_t alpha_depth_;
  };

  /** The uncompressed-BGRA codec (ColorEncoding::Bgra / BgraAlt): a pure
      channel swizzle. Stateless, a class for symmetry with the other codecs
      and to keep the swizzle off the free-function surface. */
  class RawCodec {
  public:
    /** Decode one level's BGRA bytes to RGBA8 pixels.
        @param src    the on-disk BGRA bytes; short payloads decode their
                      missing texels transparent black.
        @param width  the level width in pixels.
        @param height the level height in pixels.
        @return width * height * 4 RGBA bytes. */
    [[nodiscard]]
    Result<std::vector<std::uint8_t>> decode(std::span<const std::byte> src,
                                             std::uint32_t width,
                                             std::uint32_t height) const;

    /** Encode RGBA8 pixels to one level's BGRA bytes.
        @param rgba   width * height * 4 RGBA bytes.
        @param width  the level width in pixels.
        @param height the level height in pixels.
        @return width * height * 4 BGRA bytes. */
    [[nodiscard]]
    Result<FileBuffer> encode(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) const;
  };

  /** The mip-chain downscaler: one box-filter halving step per call, the
      standard non-sRGB-aware average every BLP toolchain uses. Stateless. */
  class MipmapScaler {
  public:
    /** Downscale a level to the next one (dimensions halve, floored at 1);
        each destination texel averages its 2x2 (or clamped 2x1/1x2/1x1)
        source footprint per channel.
        @param rgba   the source level's RGBA bytes.
        @param width  the source width in pixels.
        @param height the source height in pixels.
        @return max(1, width/2) * max(1, height/2) * 4 RGBA bytes. */
    [[nodiscard]]
    std::vector<std::uint8_t> downscale(std::span<const std::uint8_t> rgba,
                                        std::uint32_t width,
                                        std::uint32_t height) const;
  };
}
