#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include <wowlib/formats/adt/adt.hpp>

using namespace wowlib;
using namespace wowlib::formats::adt;

// The wire structs must keep their exact on-disk sizes: the serializer memcpys
// arrays of them straight from chunk payloads.
TEST_CASE("ADT wire structs have their documented on-disk sizes", "[adt][layout]")
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

// The alpha codec is a lossless decode/encode pair for the two uncompressed
// encodings; RLE decodes losslessly (re-encode is not byte-identical, by design).
TEST_CASE("ADT alpha codec round-trips the uncompressed encodings", "[adt][alpha]")
{
  std::vector<std::uint8_t> map(4096);
  for (std::size_t i = 0; i < map.size(); ++i)
    map[i] = static_cast<std::uint8_t>((i * 7) & 0xFF);

  SECTION("8-bit is exact")
  {
    std::vector<std::byte> encoded;
    wowlib::formats::adt::detail::encode_alpha_8bit(map, encoded);
    REQUIRE(encoded.size() == 4096);
    std::vector<std::uint8_t> decoded;
    wowlib::formats::adt::detail::decode_alpha_8bit(encoded, decoded);
    CHECK(decoded == map);
  }

  SECTION("4-bit is exact once quantized to nibbles")
  {
    // 4-bit stores the high nibble; decoding yields nibble * 0x11
    std::vector<std::uint8_t> quantized(4096);
    for (std::size_t i = 0; i < map.size(); ++i)
      quantized[i] = static_cast<std::uint8_t>((map[i] >> 4) * 0x11);
    std::vector<std::byte> encoded;
    wowlib::formats::adt::detail::encode_alpha_4bit(quantized, encoded);
    REQUIRE(encoded.size() == 2048);
    std::vector<std::uint8_t> decoded;
    wowlib::formats::adt::detail::decode_alpha_4bit(encoded, decoded);
    CHECK(decoded == quantized);
  }

  SECTION("RLE decodes what it encoded")
  {
    std::vector<std::byte> encoded;
    wowlib::formats::adt::detail::encode_alpha_rle(map, encoded);
    std::vector<std::uint8_t> decoded;
    wowlib::formats::adt::detail::decode_alpha_rle(encoded, decoded);
    CHECK(decoded == map);
  }
}

// A synthetic monolithic tile survives a write -> parse cycle: the derived
// header/MCIN offsets are stamped and the cell decodes back equal.
TEST_CASE("A synthetic WotLK ADT round-trips through a buffer", "[adt][roundtrip]")
{
  ADT<versions::wotlk> a;
  a.alpha_format = AlphaFormat::highres_8bit;  // 8-bit is lossless (4-bit quantizes)
  a.model_filenames.add("world\\model.m2");
  a.model_name_offsets.push_back(0);
  a.cells.assign(256, formats::adt::MapChunk<versions::wotlk>{});
  for (auto& cell : a.cells)
  {
    cell.heights.assign(145, 1.5f);
    cell.normals.assign(145, chunks::MCNREntry{{0, 127, 0}});
    cell.header.index_x = 3;
  }
  // one textured cell with a blended second layer + alpha map
  a.cells[0].layers = {chunks::SMLayer{}, chunks::SMLayer{}};
  a.cells[0].layers[1].flags =
    static_cast<std::uint32_t>(chunks::LayerFlags::use_alpha_map);
  a.cells[0].alpha_maps.assign(2, {});
  a.cells[0].alpha_maps[1].assign(4096, 128);

  const auto buf = a.write_monolithic();
  REQUIRE(buf.has_value());

  ADT<versions::wotlk> b;
  b.alpha_format = a.alpha_format;
  REQUIRE(b.parse_file(*buf, FileKind::monolithic).has_value());

  REQUIRE(b.cells.size() == 256);
  CHECK(b.cells[0].heights.size() == 145);
  CHECK(b.cells[0].heights[0] == 1.5f);
  CHECK(b.cells[0].layers.size() == 2);
  REQUIRE(b.cells[0].alpha_maps.size() == 2);
  CHECK(b.cells[0].alpha_maps[1] == a.cells[0].alpha_maps[1]);
  CHECK(b.model_filenames.entries().size() == 1);
  CHECK(b.cells[0].header.index_x == 3);
  // the derived MHDR/MCNK offsets are re-derived on write and normalized to 0
  // on read, so a valid re-parse (above) is what proves the stamping worked.
  CHECK(b.header.ofs_mcin == 0);
}
