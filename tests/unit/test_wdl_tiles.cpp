#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <span>
#include <vector>

#include <wowlib/formats/wdl/wdl.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::wdl::chunks;

namespace
{
  using Wdl = wdl::WDL<versions::Wotlk>;
  using WdlSl = wdl::WDL<versions::Shadowlands>;

  // --- synthetic buffer building ---------------------------------------------

  void putBytes(FileBuffer& b, const void* p, std::size_t n)
  {
    const auto* bytes = static_cast<const std::byte*>(p);
    b.insert(b.end(), bytes, bytes + n);
  }

  void putChunk(FileBuffer& b, const char (&cc)[5], const void* payload, std::size_t n)
  {
    const std::uint32_t fourcc = fourCc(cc);
    const auto size = static_cast<std::uint32_t>(n);
    putBytes(b, &fourcc, sizeof fourcc);
    putBytes(b, &size, sizeof size);
    putBytes(b, payload, n);
  }

  TileHeights heightsFilled(std::int16_t seed)
  {
    TileHeights h;
    for (std::size_t i = 0; i < h.outer.size(); ++i)
      h.outer[i] = static_cast<std::int16_t>(seed + static_cast<std::int16_t>(i));
    for (std::size_t i = 0; i < h.inner.size(); ++i)
      h.inner[i] = static_cast<std::int16_t>(-seed - static_cast<std::int16_t>(i));
    return h;
  }

  /** A WotLK-shaped WDL: MVER, empty object chunks, MAOF with @a slots
      nonzero markers, then per tile MARE+MAHO interleaved with real offsets. */
  FileBuffer syntheticWdl(const std::vector<std::size_t>& slots)
  {
    FileBuffer b;
    const std::uint32_t mver = wdl::WdlVersion18;
    putChunk(b, "MVER", &mver, sizeof mver);
    putChunk(b, "MWMO", nullptr, 0);
    putChunk(b, "MWID", nullptr, 0);
    putChunk(b, "MODF", nullptr, 0);

    std::vector<std::uint32_t> maof(64 * 64, 0);
    const std::size_t maofPayloadAt = b.size() + 8;
    const std::size_t tilesAt = maofPayloadAt + maof.size() * 4;
    std::size_t offset = tilesAt;
    for (const std::size_t slot : slots)
    {
      maof[slot] = static_cast<std::uint32_t>(offset);
      offset += 8 + sizeof(TileHeights) + 8 + sizeof(TileHoles);
    }
    putChunk(b, "MAOF", maof.data(), maof.size() * 4);
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
      const TileHeights h = heightsFilled(static_cast<std::int16_t>(100 * (i + 1)));
      TileHoles holes;
      holes.rows[3] = static_cast<std::uint16_t>(1u << i);
      putChunk(b, "MARE", &h, sizeof h);
      putChunk(b, "MAHO", &holes, sizeof holes);
    }
    return b;
  }

  std::vector<std::uint32_t> maofOf(std::span<const std::byte> image)
  {
    std::size_t pos = 0;
    while (image.size() - pos >= 8)
    {
      std::uint32_t fourcc = 0, size = 0;
      std::memcpy(&fourcc, image.data() + pos, 4);
      std::memcpy(&size, image.data() + pos + 4, 4);
      if (fourcc == fourCc("MAOF"))
      {
        std::vector<std::uint32_t> out(size / 4);
        std::memcpy(out.data(), image.data() + pos + 8, size);
        return out;
      }
      pos += 8 + size;
    }
    return {};
  }

  std::vector<std::uint32_t> chunkSequence(std::span<const std::byte> image)
  {
    std::vector<std::uint32_t> out;
    std::size_t pos = 0;
    while (image.size() - pos >= 8)
    {
      std::uint32_t fourcc = 0, size = 0;
      std::memcpy(&fourcc, image.data() + pos, 4);
      std::memcpy(&size, image.data() + pos + 4, 4);
      out.push_back(fourcc);
      pos += 8 + size;
    }
    return out;
  }
}

TEST_CASE("a WDL round-trips byte-perfectly and decodes its tiles", "[formats][wdl]")
{
  const FileBuffer data = syntheticWdl({100, 101, 200});

  Wdl wdl;
  REQUIRE(wdl.read(data).has_value());
  CHECK(wdl.mver == wdl::WdlVersion18);
  REQUIRE(wdl.heightmaps.size() == 3);
  REQUIRE(wdl.holes.size() == 3);
  CHECK(wdl.tileOffsets.size() == 64 * 64);
  CHECK(wdl.heightmaps[0].outer[0] == 100);
  CHECK(wdl.heightmaps[1].outer[0] == 200);
  CHECK(wdl.heightmaps[2].inner[0] == -300);
  CHECK(wdl.holes[2].rows[3] == 4);

  const auto written = wdl.write();
  REQUIRE(written.has_value());
  CHECK(*written == data);
}

TEST_CASE("adding a tile resequences the journal and restamps MAOF", "[formats][wdl]")
{
  const FileBuffer data = syntheticWdl({100, 200});

  Wdl wdl;
  REQUIRE(wdl.read(data).has_value());

  // slot 150 sits between the existing 100 and 200, so the new heightmap is
  // ordinal 1 and every stored MAOF offset after it goes stale
  wdl.tileOffsets[150] = 1;
  wdl.heightmaps.insert(wdl.heightmaps.begin() + 1, heightsFilled(7));
  wdl.holes.insert(wdl.holes.begin() + 1, TileHoles{});

  const auto written = wdl.write();
  REQUIRE(written.has_value());

  Wdl back;
  REQUIRE(back.read(*written).has_value());
  REQUIRE(back.heightmaps.size() == 3);
  CHECK(back.heightmaps[0].outer[0] == 100);
  CHECK(back.heightmaps[1].outer[0] == 7);
  CHECK(back.heightmaps[2].outer[0] == 200);

  // the emitted stream interleaves per tile: ... MAOF MARE MAHO MARE MAHO MARE MAHO
  const auto sequence = chunkSequence(*written);
  std::vector<std::uint32_t> tail(sequence.end() - 6, sequence.end());
  CHECK(tail == std::vector<std::uint32_t>{fourCc("MARE"), fourCc("MAHO"), fourCc("MARE"),
                                           fourCc("MAHO"), fourCc("MARE"), fourCc("MAHO")});

  // the stamped offsets point at the MARE chunk headers, in slot order
  const auto maof = maofOf(*written);
  REQUIRE(maof.size() == 64 * 64);
  std::vector<std::uint32_t> nonzero;
  for (const std::uint32_t v : maof)
    if (v != 0)
      nonzero.push_back(v);
  REQUIRE(nonzero.size() == 3);
  for (const std::uint32_t at : nonzero)
  {
    std::uint32_t fourcc = 0;
    std::memcpy(&fourcc, written->data() + at, 4);
    CHECK(fourcc == fourCc("MARE"));
  }
  TileHeights second{};
  std::memcpy(&second, written->data() + nonzero[1] + 8, sizeof second);
  CHECK(second.outer[0] == 7);
}

TEST_CASE("a fresh WDL emits interleaved tiles from scratch", "[formats][wdl]")
{
  Wdl wdl;
  wdl.tileOffsets.assign(64 * 64, 0);
  wdl.tileOffsets[42] = 1;
  wdl.tileOffsets[43] = 1;
  wdl.heightmaps.push_back(heightsFilled(1));
  wdl.heightmaps.push_back(heightsFilled(2));
  wdl.holes.emplace_back();
  wdl.holes.emplace_back();

  const auto written = wdl.write();
  REQUIRE(written.has_value());

  Wdl back;
  REQUIRE(back.read(*written).has_value());
  REQUIRE(back.heightmaps.size() == 2);
  CHECK(back.holes.size() == 2);
  CHECK(back.heightmaps[1].outer[0] == 2);

  const auto maof = maofOf(*written);
  REQUIRE(maof.size() == 64 * 64);
  CHECK(maof[42] != 0);
  CHECK(maof[43] != 0);
  CHECK(maof[41] == 0);

  // writing the re-read entity reproduces the fresh write byte-for-byte
  const auto again = back.write();
  REQUIRE(again.has_value());
  CHECK(*again == *written);
}

TEST_CASE("tile-table pairing violations are diagnosed", "[formats][wdl]")
{
  SECTION("nonzero slots without heightmaps")
  {
    Wdl wdl;
    wdl.tileOffsets.assign(64 * 64, 0);
    wdl.tileOffsets[7] = 1;
    const auto written = wdl.write();
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == ErrorCode::InvalidEntityState);
  }
  SECTION("a heightmap without a nonzero slot")
  {
    Wdl wdl;
    wdl.tileOffsets.assign(64 * 64, 0);
    wdl.heightmaps.push_back(heightsFilled(1));
    const auto written = wdl.write();
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == ErrorCode::InvalidEntityState);
  }
  SECTION("a short MAOF table")
  {
    Wdl wdl;
    wdl.tileOffsets.assign(16, 1);
    wdl.heightmaps.assign(16, TileHeights{});
    const auto written = wdl.write();
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == ErrorCode::InvalidEntityState);
  }
  SECTION("partial hole masks")
  {
    Wdl wdl;
    wdl.tileOffsets.assign(64 * 64, 0);
    wdl.tileOffsets[1] = 1;
    wdl.tileOffsets[2] = 1;
    wdl.heightmaps.assign(2, TileHeights{});
    wdl.holes.assign(1, TileHoles{});
    const auto written = wdl.write();
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == ErrorCode::InvalidEntityState);
  }
}

TEST_CASE("ocean masks pair through the journal interleave", "[formats][wdl]")
{
  // build a Shadowlands-shaped stream: tile 0 and 2 carry MAOE, tile 1 does not
  FileBuffer b;
  const std::uint32_t mver = wdl::WdlVersion18;
  putChunk(b, "MVER", &mver, sizeof mver);

  std::vector<std::uint32_t> maof(64 * 64, 0);
  maof[5] = maof[6] = maof[7] = 1;  // placeholder; values are restamped on write
  putChunk(b, "MAOF", maof.data(), maof.size() * 4);
  for (std::size_t i = 0; i < 3; ++i)
  {
    const TileHeights h = heightsFilled(static_cast<std::int16_t>(i + 1));
    putChunk(b, "MARE", &h, sizeof h);
    if (i != 1)
    {
      TileOcean ocean;
      ocean.mask[0] = static_cast<std::uint8_t>(0xF0 + i);
      putChunk(b, "MAOE", &ocean, sizeof ocean);
    }
    const TileHoles holes{};
    putChunk(b, "MAHO", &holes, sizeof holes);
  }

  WdlSl wdl;
  REQUIRE(wdl.read(b).has_value());
  REQUIRE(wdl.oceanMasks.size() == 2);
  CHECK(wdl.ocean_mask_tiles() == std::vector<std::uint32_t>{0, 2});

  // a content edit keeps the journal; the write replays the interleave
  const auto written = wdl.write();
  REQUIRE(written.has_value());
  const auto sequence = chunkSequence(*written);
  const std::vector<std::uint32_t> expected{
    fourCc("MVER"), fourCc("MAOF"), fourCc("MARE"), fourCc("MAOE"), fourCc("MAHO"),
    fourCc("MARE"), fourCc("MAHO"), fourCc("MARE"), fourCc("MAOE"), fourCc("MAHO")};
  CHECK(sequence == expected);

  // adding a tile at the end rebuilds the journal but keeps the pairing
  // (hole masks are all-or-nothing, so the new tile needs one too)
  wdl.tileOffsets[100] = 1;
  wdl.heightmaps.push_back(heightsFilled(9));
  wdl.holes.emplace_back();
  const auto rebuilt = wdl.write();
  REQUIRE(rebuilt.has_value());
  WdlSl back;
  REQUIRE(back.read(*rebuilt).has_value());
  CHECK(back.ocean_mask_tiles() == std::vector<std::uint32_t>{0, 2});
  const auto resequenced = chunkSequence(*rebuilt);
  const std::vector<std::uint32_t> expected2{
    fourCc("MVER"), fourCc("MAOF"), fourCc("MARE"), fourCc("MAOE"), fourCc("MAHO"),
    fourCc("MARE"), fourCc("MAHO"), fourCc("MARE"), fourCc("MAOE"), fourCc("MAHO"),
    fourCc("MARE"), fourCc("MAHO")};
  CHECK(resequenced == expected2);
}

TEST_CASE("a WDT root round-trips and gates its chunks by era", "[formats][wdt]")
{
  using RootOld = wdt::root::WDTRoot<versions::Wotlk>;

  FileBuffer b;
  const std::uint32_t mver = wdt::WdtVersion18;
  putChunk(b, "MVER", &mver, sizeof mver);
  wdt::root::chunks::SMMapHeader<versions::Wotlk> header{};
  header.flags = 0x2;
  putChunk(b, "MPHD", &header, sizeof header);
  std::vector<wdt::root::chunks::SMAreaInfo> tiles(64 * 64);
  tiles[64 * 32 + 32].flags = 0x1;
  putChunk(b, "MAIN", tiles.data(), tiles.size() * sizeof tiles[0]);
  putChunk(b, "MWMO", nullptr, 0);

  RootOld root;
  REQUIRE(root.read(b).has_value());
  CHECK(root.header.flags == 0x2);
  REQUIRE(root.tiles.size() == 64 * 64);
  CHECK(root.tiles[64 * 32 + 32].flags == 0x1);
  CHECK(root.globalWmoName.empty());
  CHECK(root.globalWmo.empty());

  const auto written = root.write();
  REQUIRE(written.has_value());
  CHECK(*written == b);
}

TEST_CASE("repeated _mpv groups keep their interleave through the journal",
          "[formats][wdt]")
{
  using Mpv = wdt::mpv::WDTParticulates<versions::Shadowlands>;

  FileBuffer b;
  const std::uint32_t mver = 4;
  putChunk(b, "MVER", &mver, sizeof mver);
  const std::vector<std::byte> mi(0x10D8, std::byte{0x11});
  wdt::mpv::chunks::ParticulatePoint point{};
  point.unkC = 1.5f;
  wdt::mpv::chunks::ParticulateBounds bounds{};
  bounds.pointCount = 1;
  for (int group = 0; group < 2; ++group)
  {
    putChunk(b, "PVMI", mi.data(), mi.size());
    putChunk(b, "PVPD", &point, sizeof point);
    putChunk(b, "PVBD", &bounds, sizeof bounds);
  }

  Mpv mpv;
  REQUIRE(mpv.read(b).has_value());
  REQUIRE(mpv.volumeData.size() == 2);
  REQUIRE(mpv.pointGroups.size() == 2);
  REQUIRE(mpv.boundGroups.size() == 2);
  CHECK(mpv.pointGroups[1][0].unkC == 1.5f);

  const auto written = mpv.write();
  REQUIRE(written.has_value());
  CHECK(*written == b);
}
