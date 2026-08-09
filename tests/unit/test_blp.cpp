#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <wowlib/formats/blp/blp.hpp>
#include <wowlib/formats/blp/codec.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::blp;

namespace
{
  // --- synthetic image building ----------------------------------------------

  /** A deterministic RGBA test card: per-pixel channel ramps + a transparent
      quadrant when punch_hole is set. */
  Image test_card(std::uint32_t width, std::uint32_t height, bool punch_hole = false)
  {
    Image image{.width = width, .height = height, .pixels = {}};
    image.pixels.resize(std::size_t{width} * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
      for (std::uint32_t x = 0; x < width; ++x)
      {
        const std::size_t at = (std::size_t{y} * width + x) * 4;
        image.pixels[at + 0] = static_cast<std::uint8_t>((x * 255) / std::max(1u, width - 1));
        image.pixels[at + 1] = static_cast<std::uint8_t>((y * 255) / std::max(1u, height - 1));
        image.pixels[at + 2] = static_cast<std::uint8_t>(((x + y) * 7) % 256);
        image.pixels[at + 3] =
          punch_hole && x < width / 2 && y < height / 2 ? 0 : std::uint8_t{255};
      }
    return image;
  }

  /** Maximum absolute per-channel error between two equally-sized images. */
  int max_channel_error(const Image& a, const Image& b)
  {
    REQUIRE(a.pixels.size() == b.pixels.size());
    int worst = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i)
      worst = std::max(worst, std::abs(int{a.pixels[i]} - int{b.pixels[i]}));
    return worst;
  }

  /** write() -> read() -> the reread entity, asserting the bytes survive. */
  BLP reread(const BLP& blp)
  {
    const auto bytes = blp.write();
    REQUIRE(bytes.has_value());
    BLP back;
    REQUIRE(back.read(*bytes).has_value());
    const auto again = back.write();
    REQUIRE(again.has_value());
    CHECK(*again == *bytes);
    return back;
  }
}

TEST_CASE("the BLP2 header layout matches the on-disk format", "[formats][blp]")
{
  STATIC_CHECK(sizeof(blp::detail::BLPHeader) == 0x94);
  STATIC_CHECK(offsetof(blp::detail::BLPHeader, width) == 0x0C);
  STATIC_CHECK(offsetof(blp::detail::BLPHeader, mip_offsets) == 0x14);
  STATIC_CHECK(offsetof(blp::detail::BLPHeader, mip_sizes) == 0x54);
  STATIC_CHECK(blp_header_bytes == 0x94 + blp_palette_size * sizeof(CImVector));
  STATIC_CHECK(blp_magic == 0x32504C42);  // 'BLP2' little-endian
}

TEST_CASE("BGRA encoding round-trips pixels exactly", "[formats][blp]")
{
  const Image card = test_card(16, 8);
  BLP blp;
  REQUIRE(blp.encode(card, {.encoding = ColorEncoding::Bgra}).has_value());
  CHECK(blp.color_encoding == ColorEncoding::Bgra);
  CHECK(blp.preferred_format == PixelFormat::Argb8888);
  CHECK(blp.width == 16);
  CHECK(blp.height == 8);
  CHECK(blp.mip_count() == 5);  // 16x8, 8x4, 4x2, 2x1, 1x1

  const BLP back = reread(blp);
  const auto decoded = back.decode();
  REQUIRE(decoded.has_value());
  CHECK(max_channel_error(card, *decoded) == 0);

  // every level decodes at halved dimensions
  for (std::uint32_t level = 0; level < back.mip_count(); ++level)
  {
    const auto mip = back.decode(level);
    REQUIRE(mip.has_value());
    CHECK(mip->width == back.mip_width(level));
    CHECK(mip->height == back.mip_height(level));
  }
}

TEST_CASE("palettized encoding is exact for images within the palette budget",
          "[formats][blp]")
{
  // 16 distinct colors + 8-bit alpha: quantization must be lossless
  Image card = test_card(8, 8);
  for (std::uint32_t y = 0; y < 8; ++y)
    for (std::uint32_t x = 0; x < 8; ++x)
    {
      const std::size_t at = (std::size_t{y} * 8 + x) * 4;
      card.pixels[at + 0] = static_cast<std::uint8_t>((x % 4) * 64);
      card.pixels[at + 1] = static_cast<std::uint8_t>((y % 4) * 64);
      card.pixels[at + 2] = 100;
      card.pixels[at + 3] = static_cast<std::uint8_t>(y * 30);
    }

  BLP blp;
  REQUIRE(
    blp.encode(card, {.encoding = ColorEncoding::Palettized, .alpha_depth = 8}).has_value());
  CHECK(blp.color_encoding == ColorEncoding::Palettized);
  CHECK(blp.alpha_depth == 8);

  const auto decoded = reread(blp).decode();
  REQUIRE(decoded.has_value());
  CHECK(max_channel_error(card, *decoded) == 0);
}

TEST_CASE("palettized 1-bit and 4-bit alpha planes pack and unpack", "[formats][blp]")
{
  Image card = test_card(8, 4);
  for (std::size_t i = 0; i < card.pixels.size() / 4; ++i)
    card.pixels[i * 4 + 3] = i % 2 ? 255 : 0;

  BLP one_bit;
  REQUIRE(one_bit.encode(card, {.encoding = ColorEncoding::Palettized, .alpha_depth = 1,
                                .mipmaps = false})
            .has_value());
  const auto decoded_1 = reread(one_bit).decode();
  REQUIRE(decoded_1.has_value());
  for (std::size_t i = 0; i < card.pixels.size() / 4; ++i)
    CHECK(decoded_1->pixels[i * 4 + 3] == (i % 2 ? 255 : 0));

  // 4-bit: alpha values that are exact multiples of 0x11 survive verbatim
  for (std::size_t i = 0; i < card.pixels.size() / 4; ++i)
    card.pixels[i * 4 + 3] = static_cast<std::uint8_t>((i % 16) * 0x11);
  BLP four_bit;
  REQUIRE(four_bit.encode(card, {.encoding = ColorEncoding::Palettized, .alpha_depth = 4,
                                 .mipmaps = false})
            .has_value());
  const auto decoded_4 = reread(four_bit).decode();
  REQUIRE(decoded_4.has_value());
  for (std::size_t i = 0; i < card.pixels.size() / 4; ++i)
    CHECK(decoded_4->pixels[i * 4 + 3] == (i % 16) * 0x11);
}

TEST_CASE("palette quantization stays close beyond 256 distinct colors", "[formats][blp]")
{
  // 400 distinct colors on a smooth two-axis ramp: the median cut must land
  // every pixel within a couple of ramp steps
  Image card{.width = 20, .height = 20, .pixels = {}};
  card.pixels.resize(20 * 20 * 4);
  for (std::uint32_t y = 0; y < 20; ++y)
    for (std::uint32_t x = 0; x < 20; ++x)
    {
      const std::size_t at = (std::size_t{y} * 20 + x) * 4;
      card.pixels[at + 0] = static_cast<std::uint8_t>((x * 255) / 19);
      card.pixels[at + 1] = static_cast<std::uint8_t>((y * 255) / 19);
      card.pixels[at + 2] = 77;
      card.pixels[at + 3] = 255;
    }

  BLP blp;
  REQUIRE(
    blp.encode(card, {.encoding = ColorEncoding::Palettized, .alpha_depth = 0}).has_value());
  const auto decoded = blp.decode();
  REQUIRE(decoded.has_value());
  CHECK(max_channel_error(card, *decoded) <= 24);
  for (std::size_t i = 3; i < decoded->pixels.size(); i += 4)
    CHECK(decoded->pixels[i] == 255);  // depth 0: fully opaque
}

TEST_CASE("DXT1 compresses opaque blocks and punches through transparent ones",
          "[formats][blp]")
{
  const Image card = test_card(16, 16, /*punch_hole=*/true);
  BLP blp;
  REQUIRE(blp.encode(card, {.format = PixelFormat::Dxt1, .alpha_depth = 1}).has_value());
  CHECK(blp.preferred_format == PixelFormat::Dxt1);
  CHECK(blp.alpha_depth == 1);
  CHECK(blp.mips[0].size() == 16 / 4 * 16 / 4 * 8);

  const auto decoded = reread(blp).decode();
  REQUIRE(decoded.has_value());
  for (std::uint32_t y = 0; y < 16; ++y)
    for (std::uint32_t x = 0; x < 16; ++x)
    {
      const std::uint8_t alpha = decoded->pixels[(std::size_t{y} * 16 + x) * 4 + 3];
      CHECK(alpha == (x < 8 && y < 8 ? 0 : 255));
    }
  // color comparison only where opaque: punch-through texels decode black
  int worst = 0;
  for (std::size_t i = 0; i < card.pixels.size(); i += 4)
    if (card.pixels[i + 3] >= 128)
      for (std::size_t ch = 0; ch < 3; ++ch)
        worst = std::max(worst,
                         std::abs(int{card.pixels[i + ch]} - int{decoded->pixels[i + ch]}));
  CHECK(worst <= 40);  // block compression budget
}

TEST_CASE("DXT3 and DXT5 carry alpha through compression", "[formats][blp]")
{
  Image card = test_card(16, 16);
  for (std::size_t i = 0; i < card.pixels.size() / 4; ++i)
    card.pixels[i * 4 + 3] = static_cast<std::uint8_t>((i * 255) / (card.pixels.size() / 4 - 1));

  for (const PixelFormat format : {PixelFormat::Dxt3, PixelFormat::Dxt5})
  {
    BLP blp;
    REQUIRE(blp.encode(card, {.format = format}).has_value());
    CHECK(blp.preferred_format == format);
    CHECK(blp.mips[0].size() == 16 / 4 * 16 / 4 * 16);

    const auto decoded = reread(blp).decode();
    REQUIRE(decoded.has_value());
    CHECK(max_channel_error(card, *decoded) <= 40);
  }
}

TEST_CASE("BC5 keeps two channels", "[formats][blp]")
{
  const Image card = test_card(8, 8);
  BLP blp;
  REQUIRE(blp.encode(card, {.format = PixelFormat::Bc5, .mipmaps = false}).has_value());

  const auto decoded = reread(blp).decode();
  REQUIRE(decoded.has_value());
  for (std::size_t i = 0; i < card.pixels.size() / 4; ++i)
  {
    CHECK(std::abs(int{decoded->pixels[i * 4 + 0]} - int{card.pixels[i * 4 + 0]}) <= 8);
    CHECK(std::abs(int{decoded->pixels[i * 4 + 1]} - int{card.pixels[i * 4 + 1]}) <= 8);
    CHECK(decoded->pixels[i * 4 + 2] == 0);
    CHECK(decoded->pixels[i * 4 + 3] == 255);
  }
}

TEST_CASE("a hand-built BC1 block decodes to the reference colors", "[formats][blp]")
{
  // red/blue endpoints, one row per index: c0, c1, 2/3-1/3 blends
  BLP blp;
  blp.color_encoding = ColorEncoding::Dxt;
  blp.preferred_format = PixelFormat::Dxt1;
  blp.alpha_depth = 0;
  blp.width = 4;
  blp.height = 4;
  blp.mip_flags = 0;

  FileBuffer block;
  const auto push16 = [&](std::uint16_t v) {
    block.push_back(static_cast<std::byte>(v & 0xFF));
    block.push_back(static_cast<std::byte>(v >> 8));
  };
  push16(0xF800);  // color0: pure red   (c0 > c1: 4-color mode)
  push16(0x001F);  // color1: pure blue
  block.push_back(static_cast<std::byte>(0b00000000));  // row 0: all index 0
  block.push_back(static_cast<std::byte>(0b01010101));  // row 1: all index 1
  block.push_back(static_cast<std::byte>(0b10101010));  // row 2: all index 2
  block.push_back(static_cast<std::byte>(0b11111111));  // row 3: all index 3
  REQUIRE(blp.set_mip(0, block).has_value());

  const auto decoded = blp.decode();
  REQUIRE(decoded.has_value());
  const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
    return std::span{decoded->pixels}.subspan((std::size_t{y} * 4 + x) * 4, 4);
  };
  CHECK(pixel(0, 0)[0] == 255);  // c0 = red
  CHECK(pixel(0, 0)[2] == 0);
  CHECK(pixel(0, 1)[0] == 0);  // c1 = blue
  CHECK(pixel(0, 1)[2] == 255);
  CHECK(pixel(0, 2)[0] == 170);  // (2*255 + 0) / 3
  CHECK(pixel(0, 2)[2] == 85);   // (2*0 + 255) / 3
  CHECK(pixel(0, 3)[0] == 85);
  CHECK(pixel(0, 3)[2] == 170);
  for (std::uint32_t y = 0; y < 4; ++y)
    CHECK(pixel(0, y)[3] == 255);
}

TEST_CASE("unusual on-disk layouts replay byte-perfectly", "[formats][blp]")
{
  // craft a file with a gap between the header region and mip 0, reversed
  // level placement and a trailing tail
  blp::detail::BLPHeader header{};
  header.color_encoding = std::to_underlying(ColorEncoding::Bgra);
  header.alpha_depth = 8;
  header.preferred_format = std::to_underlying(PixelFormat::Argb8888);
  header.mip_flags = 1;
  header.width = 2;
  header.height = 1;

  const std::size_t gap = 7;
  const FileBuffer mip1(4, std::byte{0xAA});                  // 1x1 BGRA
  const FileBuffer mip0(8, std::byte{0x5B});                  // 2x1 BGRA
  header.mip_offsets[1] = static_cast<std::uint32_t>(blp_header_bytes + gap);
  header.mip_sizes[1] = static_cast<std::uint32_t>(mip1.size());
  header.mip_offsets[0] = header.mip_offsets[1] + header.mip_sizes[1];
  header.mip_sizes[0] = static_cast<std::uint32_t>(mip0.size());

  FileBuffer file(blp_header_bytes + gap + mip0.size() + mip1.size(), std::byte{0xEE});
  std::memcpy(file.data(), &header, sizeof header);
  std::memcpy(file.data() + header.mip_offsets[1], mip1.data(), mip1.size());
  std::memcpy(file.data() + header.mip_offsets[0], mip0.data(), mip0.size());
  file.push_back(std::byte{0xDE});  // trailing tail
  file.push_back(std::byte{0xAD});

  BLP blp;
  REQUIRE(blp.read(file).has_value());
  CHECK(blp.mip_count() == 2);

  const auto rewritten = blp.write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);

  // same-size payload replacement keeps the replayed placement
  const FileBuffer patched(8, std::byte{0x11});
  REQUIRE(blp.set_mip(0, patched).has_value());
  const auto same_size = blp.write();
  REQUIRE(same_size.has_value());
  CHECK(same_size->size() == file.size());
  CHECK(std::memcmp(same_size->data() + header.mip_offsets[0], patched.data(), 8) == 0);

  // a size change falls back to the canonical contiguous layout
  const FileBuffer grown(12, std::byte{0x22});
  REQUIRE(blp.set_mip(0, grown).has_value());
  const auto canonical = blp.write();
  REQUIRE(canonical.has_value());
  CHECK(canonical->size() == blp_header_bytes + grown.size() + mip1.size());
}

TEST_CASE("decode and encode diagnose invalid inputs", "[formats][blp]")
{
  BLP blp;
  CHECK(blp.decode().error().code == ErrorCode::InvalidEntityState);
  CHECK(blp.mip(0).error().code == ErrorCode::InvalidEntityState);

  const Image empty{};
  CHECK(blp.encode(empty).error().code == ErrorCode::InvalidEntityState);

  Image mismatched{.width = 4, .height = 4, .pixels = {}};
  mismatched.pixels.resize(7);
  CHECK(blp.encode(mismatched).error().code == ErrorCode::InvalidEntityState);

  const Image card = test_card(4, 4);
  CHECK(blp.encode(card, {.encoding = ColorEncoding::Jpeg}).error().code
        == ErrorCode::NotSupported);
  CHECK(blp.encode(card, {.alpha_depth = 3}).error().code == ErrorCode::InvalidEntityState);

  FileBuffer not_blp(blp_header_bytes, std::byte{0});
  CHECK(blp.read(not_blp).error().code == ErrorCode::FormatVersionMismatch);
  FileBuffer tiny(16, std::byte{0});
  CHECK(blp.read(tiny).error().code == ErrorCode::ChunkTruncated);
}

TEST_CASE("non-power-of-two and tall/wide mip chains stay well-formed", "[formats][blp]")
{
  const Image card = test_card(10, 3);
  BLP blp;
  REQUIRE(blp.encode(card, {.encoding = ColorEncoding::Bgra}).has_value());
  // 10x3 -> 5x1 -> 2x1 -> 1x1
  CHECK(blp.mip_count() == 4);
  CHECK(blp.mip_width(1) == 5);
  CHECK(blp.mip_height(1) == 1);

  const auto decoded = reread(blp).decode(3);
  REQUIRE(decoded.has_value());
  CHECK(decoded->width == 1);
  CHECK(decoded->height == 1);
}

TEST_CASE("blp: validate() checks the mip chain and dimensions",
          "[formats][blp][validation]")
{
  BLP blp;
  blp.color_encoding = ColorEncoding::Bgra;
  blp.preferred_format = PixelFormat::Argb8888;
  blp.alpha_depth = 8;
  blp.width = 4;
  blp.height = 4;
  blp.mips.emplace_back(4 * 4 * 4, std::byte{0});
  CHECK(blp.validate().ok());

  SECTION("a texture needs its base level")
  {
    blp.mips.clear();
    CHECK_FALSE(blp.validate().ok());
  }

  SECTION("zero dimensions cannot size a texture")
  {
    blp.width = 0;
    CHECK_FALSE(blp.validate().ok());
  }

  SECTION("more levels than the header can address")
  {
    blp.mips.assign(blp_max_mips + 1, FileBuffer(4 * 4 * 4, std::byte{0}));
    CHECK_FALSE(blp.validate().ok());
  }

  SECTION("a short raw level only warns — the decoders pad it")
  {
    blp.mips[0].resize(4);
    const auto report = blp.validate();
    CHECK(report.ok());
    CHECK(report.warning_count() == 1);
  }

  SECTION("a short palettized level errors — the client would read past it")
  {
    blp.color_encoding = ColorEncoding::Palettized;
    blp.alpha_depth = 0;
    blp.mips[0].resize(4);  // needs 4*4 indices
    CHECK_FALSE(blp.validate().ok());
  }

  SECTION("a junk alpha depth is fatal only where it sizes a plane")
  {
    blp.alpha_depth = 136;  // as 3.3.5a Textures/SunGlare.blp ships it
    CHECK(blp.validate().ok());
    CHECK(blp.validate().warning_count() == 1);

    blp.color_encoding = ColorEncoding::Palettized;
    CHECK_FALSE(blp.validate().ok());
  }
}
