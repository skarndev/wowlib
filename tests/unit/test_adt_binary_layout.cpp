#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include <wowlib/formats/adt/adt.hpp>

using namespace wowlib;
using namespace wowlib::formats::adt;

// The binary structs must keep their exact on-disk sizes: the serializer memcpys
// arrays of them straight from chunk payloads.
TEST_CASE("ADT binary structs have their documented on-disk sizes", "[adt][layout]")
{
  STATIC_REQUIRE(sizeof(chunks::SMChunk) == 0x80);
  STATIC_REQUIRE(sizeof(chunks::MHDRData) == 0x40);
  STATIC_REQUIRE(sizeof(chunks::MFBOPlanes) == 36);
  STATIC_REQUIRE(sizeof(chunks::SMLayer) == 0x10);
  STATIC_REQUIRE(sizeof(chunks::SMTextureFlags) == 0x4);
  STATIC_REQUIRE(sizeof(chunks::SMTextureParams) == 0x10);
  STATIC_REQUIRE(sizeof(chunks::SMTextureColorGrading) == 0x10);
  STATIC_REQUIRE(sizeof(chunks::MCNREntry) == 3);
  STATIC_REQUIRE(sizeof(chunks::CWSoundEmitter) == 0x1C);
  STATIC_REQUIRE(sizeof(chunks::SLVert) == 8);
  STATIC_REQUIRE(sizeof(chunks::SMVert) == 8);
  STATIC_REQUIRE(sizeof(chunks::SWFlowv) == 40);
  STATIC_REQUIRE(sizeof(chunks::UVMapEntry) == 4);
}

// AlphaMapCodec is a lossless decode/encode pair for the two uncompressed
// encodings; RLE decodes losslessly (re-encode is not byte-identical, by design).
TEST_CASE("ADT alpha codec round-trips the uncompressed encodings", "[adt][alpha]")
{
  std::vector<std::uint8_t> map(4096);
  for (std::size_t i = 0; i < map.size(); ++i)
    map[i] = static_cast<std::uint8_t>((i * 7) & 0xFF);

  SECTION("8-bit is exact")
  {
    const wowlib::formats::adt::detail::AlphaMapCodec codec{AlphaFormat::Highres8Bit};
    std::vector<std::byte> encoded;
    codec.encode(map, /*compressed=*/false, encoded);
    REQUIRE(encoded.size() == 4096);
    const auto decoded = codec.decode(encoded, /*compressed=*/false, /*fixEdges=*/false);
    CHECK(decoded == map);
  }

  SECTION("4-bit is exact once quantized to nibbles")
  {
    // 4-bit stores the high nibble; decoding yields nibble * 0x11
    std::vector<std::uint8_t> quantized(4096);
    for (std::size_t i = 0; i < map.size(); ++i)
      quantized[i] = static_cast<std::uint8_t>((map[i] >> 4) * 0x11);
    const wowlib::formats::adt::detail::AlphaMapCodec codec{AlphaFormat::Lowres4Bit};
    std::vector<std::byte> encoded;
    codec.encode(quantized, /*compressed=*/false, encoded);
    REQUIRE(encoded.size() == 2048);
    const auto decoded = codec.decode(encoded, /*compressed=*/false, /*fixEdges=*/false);
    CHECK(decoded == quantized);
  }

  SECTION("RLE decodes what it encoded")
  {
    // compression is independent of the bit depth; the format is irrelevant here.
    const wowlib::formats::adt::detail::AlphaMapCodec codec{AlphaFormat::Highres8Bit};
    std::vector<std::byte> encoded;
    codec.encode(map, /*compressed=*/true, encoded);
    const auto decoded = codec.decode(encoded, /*compressed=*/true, /*fixEdges=*/false);
    CHECK(decoded == map);
  }
}

// A synthetic monolithic tile survives a write -> parse cycle: the derived
// header/MCIN offsets are stamped and the chunk decodes back equal.
TEST_CASE("A synthetic WotLK ADT round-trips through a buffer", "[adt][roundtrip]")
{
  ADT<versions::Wotlk> a;
  a.alphaFormat = AlphaFormat::Highres8Bit;  // 8-bit is lossless (4-bit quantizes)
  a.modelFilenames.add("world\\model.m2");
  a.modelNameOffsets.push_back(0);
  a.chunks.assign(256, formats::adt::MapChunk<versions::Wotlk>{});
  for (auto& chunk : a.chunks)
  {
    chunk.heights.assign(145, 1.5f);
    chunk.normals.assign(145, chunks::MCNREntry{{0, 127, 0}});
    chunk.header.indexX = 3;
  }
  // one textured chunk with a blended second layer + alpha map
  a.chunks[0].layers = {chunks::SMLayer{}, chunks::SMLayer{}};
  a.chunks[0].layers[1].flags = static_cast<std::uint32_t>(chunks::LayerFlags::UseAlphaMap);
  a.chunks[0].alphaMaps.assign(2, {});
  a.chunks[0].alphaMaps[1].assign(4096, 128);

  const auto buf = a.writeFile(FileKind::Monolithic, a.alphaFormat);
  REQUIRE(buf.has_value());

  ADT<versions::Wotlk> b;
  b.alphaFormat = a.alphaFormat;
  REQUIRE(b.parse_file(*buf, FileKind::Monolithic).has_value());

  REQUIRE(b.chunks.size() == 256);
  CHECK(b.chunks[0].heights.size() == 145);
  CHECK(b.chunks[0].heights[0] == 1.5f);
  CHECK(b.chunks[0].layers.size() == 2);
  REQUIRE(b.chunks[0].alphaMaps.size() == 2);
  CHECK(b.chunks[0].alphaMaps[1] == a.chunks[0].alphaMaps[1]);
  CHECK(b.modelFilenames.entries().size() == 1);
  CHECK(b.chunks[0].header.indexX == 3);
  // the derived MHDR/MCNK offsets are re-derived on write and normalized to 0
  // on read, so a valid re-parse (above) is what proves the stamping worked.
  CHECK(b.header.ofsMcin == 0);
}
