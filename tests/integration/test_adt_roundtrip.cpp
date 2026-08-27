#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/wdt/wdt.hpp>
#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::formats;

namespace
{
  /** The first bit-level divergence between two decoded values, as a member
      path, or nullopt when identical. Bitwise (memcmp) at the leaves so NaN
      floats in client heights — bit-preserved by the round-trip — do not read
      as divergence (the M2 diffValue pattern, generalized over the reflected
      member tree so it walks StringBlock, the liquid entities and the trait
      bases too). */
  template <typename T>
  std::optional<std::string> diffValue(const T& a, const T& b)
  {
    if constexpr (formats::detail::IsVectorV<T>)
    {
      if (a.size() != b.size())
        return std::format(": size {} vs {}", a.size(), b.size());
      for (std::size_t i = 0; i < a.size(); ++i)
        if (auto d = diffValue(a[i], b[i]))
          return std::format("[{}]{}", i, *d);
      return std::nullopt;
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
      if (a != b)
        return std::format(": \"{}\" vs \"{}\"", a, b);
      return std::nullopt;
    }
    else if constexpr (std::is_trivially_copyable_v<T>)
    {
      if (std::memcmp(&a, &b, sizeof(T)) != 0)
        return std::optional<std::string>{": bytes differ"};
      return std::nullopt;
    }
    else
    {
      static constexpr auto Members = formats::detail::membersOf<T>();
      std::optional<std::string> out;
      template for (constexpr auto m : Members)
      {
        if (!out)
          if (auto d = diffValue(a.[:m:], b.[:m:]))
            out = std::format(".{}{}", std::meta::identifier_of(m), *d);
      }
      return out;
    }
  }

  /** The on-disk alpha bit depth a map's WDT MPHD flags select: 4096-byte 8-bit
      maps when AdtHasBigAlpha (0x4) or AdtHasHeightTexturing (0x80) is set,
      else 2048-byte 4-bit. wowlib does not resolve this itself — the caller reads
      the WDT (as this test does) and passes the format to ADT read()/write(). */
  adt::AlphaFormat alphaFormatOf(std::uint32_t mphdFlags)
  {
    return (mphdFlags & 0x4) || (mphdFlags & 0x80) ? adt::AlphaFormat::Highres8Bit
                                                     : adt::AlphaFormat::Lowres4Bit;
  }

  /** Structural invariants every decoded chunk must satisfy — a guard against a
      SILENT misparse (a stream misalignment that a semantic round-trip cannot
      catch, since both sides misparse identically). A chunk either has a full
      terrain grid or none; alpha/shadow maps are the full 64x64 edit surface;
      the alpha-map list is aligned with the layers. */
  template <typename Chunk>
  void checkChunk(const Chunk& c, std::size_t index, const std::string& label)
  {
    INFO(label << " chunk " << index);
    CHECK((c.heights.empty() || c.heights.size() == 145));
    CHECK((c.normals.empty() || c.normals.size() == 145));
    CHECK((c.shadowMap.empty() || c.shadowMap.size() == 4096));
    CHECK(c.alphaMaps.size() == c.layers.size());
    for (const auto& map : c.alphaMaps)
      CHECK((map.empty() || map.size() == 4096));
    if constexpr (requires { c.vertexColors; })
      CHECK((c.vertexColors.empty() || c.vertexColors.size() == 145));
  }

  /** Semantic round-trip of one monolithic (pre-Cata) tile: read it from the
      client, canonically rewrite to a buffer, parse the buffer back with the
      same alpha format, and require decoded equality (ADT is not byte-perfect —
      see adt-architecture). */
  template <ClientVersion V>
  void roundtripAdt(fs::FileSystem& fs, const FileKey& key, adt::AlphaFormat af,
                     const std::string& label)
  {
    INFO(label);
    adt::ADT<V> a;
    {
      const auto r = a.read(fs, key, af);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    CHECK(a.mver == adt::AdtVersion18);
    REQUIRE(a.chunks.size() == 256);
    for (std::size_t i = 0; i < a.chunks.size(); ++i)
      checkChunk(a.chunks[i], i, label);
    // a freshly read, unmodified client tile passes validation with zero
    // errors (warnings are allowed - they mark states real files ship)
    if (const auto valid = a.ensureValid(); !valid)
      FAIL(std::format("{}: {}", label, valid.error().message));

    const auto buf = a.writeFile(adt::FileKind::Monolithic, af);
    REQUIRE(buf.has_value());

    adt::ADT<V> b;
    b.alphaFormat = af;
    {
      const auto r = b.parse_file(*buf, adt::FileKind::Monolithic);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }

    const auto d = diffValue(a, b);
    INFO(d.value_or(""));
    CHECK_FALSE(d.has_value());
  }

  /** Semantic round-trip of one Cata+ split tile: read (root + _tex0 + _obj0
      merged, _obj1/_lod preserved verbatim), rewrite each physical file to a
      buffer, parse them all back into a fresh entity, and require decoded
      equality. */
  template <ClientVersion V>
  void roundtripAdtSplit(fs::FileSystem& fs, const FileKey& key, adt::AlphaFormat af,
                           const std::string& label)
  {
    INFO(label);
    adt::ADT<V> a;
    {
      const auto r = a.read(fs, key, af);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    REQUIRE(a.chunks.size() == 256);
    for (std::size_t i = 0; i < a.chunks.size(); ++i)
      checkChunk(a.chunks[i], i, label);
    if (const auto valid = a.ensureValid(); !valid)
      FAIL(std::format("{}: {}", label, valid.error().message));

    adt::ADT<V> b;
    b.alphaFormat = af;
    b.chunks.assign(256, adt::MapChunk<V>{});
    for (const auto kind : {adt::FileKind::Root, adt::FileKind::Tex0, adt::FileKind::Obj0})
    {
      const auto buf = a.writeFile(kind, af);
      REQUIRE(buf.has_value());
      const auto r = b.parse_file(*buf, kind);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    // _obj1/_lod are round-tripped verbatim by write(); mirror that here
    b.obj1Data = a.obj1Data;
    b.lodData = a.lodData;

    const auto d = diffValue(a, b);
    INFO(d.value_or(""));
    CHECK_FALSE(d.has_value());
  }
}

TEST_CASE("3.3.5a ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::mpqClient(),
                                  .version = versions::Wotlk});
  REQUIRE(fs.has_value());

  const std::vector<std::string> maps{
    "Azeroth", "Kalimdor", "Northrend", "Expansion01", "PVPZone01", "Ulduar",
  };

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdtPath = std::format("World/Maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdtPath))
    {
      WARN("not in client, skipped: " + wdtPath);
      continue;
    }
    wdt::root::WDTRoot<versions::Wotlk> root;
    REQUIRE(root.read(*fs->readFile(FileKey{wdtPath})).has_value());
    const auto af = alphaFormatOf(root.header.flags);

    int tilesThisMap = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tilesThisMap < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("World/Maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtripAdt<versions::Wotlk>(*fs, FileKey{adt}, af, adt);
      ++tilesThisMap;
      ++verified;
    }
  }
  CHECK(verified >= 6);
}

TEST_CASE("1.12.2 ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::vanillaClient(),
                                  .version = versions::Vanilla,
                                  .locale = tests::vanillaLocale()});
  REQUIRE(fs.has_value());

  const std::vector<std::string> maps{"Azeroth", "Kalimdor"};

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdtPath = std::format("World/Maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdtPath))
    {
      WARN("not in client, skipped: " + wdtPath);
      continue;
    }
    wdt::root::WDTRoot<versions::Vanilla> root;
    REQUIRE(root.read(*fs->readFile(FileKey{wdtPath})).has_value());
    const auto af = alphaFormatOf(root.header.flags);

    int tilesThisMap = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tilesThisMap < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("World/Maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtripAdt<versions::Vanilla>(*fs, FileKey{adt}, af, adt);
      ++tilesThisMap;
      ++verified;
    }
  }
  CHECK(verified >= 2);
}

TEST_CASE("2.4.3 ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::tbcClient(),
                                  .version = versions::Tbc,
                                  .locale = tests::tbcLocale()});
  REQUIRE(fs.has_value());

  // the two vanilla continents plus Outland (Expansion01), TBC's new continent
  const std::vector<std::string> maps{"Azeroth", "Kalimdor", "Expansion01"};

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdtPath = std::format("World/Maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdtPath))
    {
      WARN("not in client, skipped: " + wdtPath);
      continue;
    }
    wdt::root::WDTRoot<versions::Tbc> root;
    REQUIRE(root.read(*fs->readFile(FileKey{wdtPath})).has_value());
    const auto af = alphaFormatOf(root.header.flags);

    int tilesThisMap = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tilesThisMap < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("World/Maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtripAdt<versions::Tbc>(*fs, FileKey{adt}, af, adt);
      ++tilesThisMap;
      ++verified;
    }
  }
  CHECK(verified >= 3);
}

TEST_CASE("9.2.7 split ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  const auto listfile = tests::requireListfile();
  auto fs = fs::FileSystem::open({.clientPath = tests::cascClient(),
                                  .version = versions::Shadowlands,
                                  .listfileCsv = listfile});
  REQUIRE(fs.has_value());

  const std::vector<std::string> maps{"kultiras", "azeroth", "kalimdor"};

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdtPath = std::format("world/maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdtPath))
    {
      WARN("not in client, skipped: " + wdtPath);
      continue;
    }
    wdt::root::WDTRoot<versions::Shadowlands> root;
    const auto raw = fs->readFile(FileKey{wdtPath});
    REQUIRE(raw.has_value());
    REQUIRE(root.read(*raw).has_value());
    const auto af = alphaFormatOf(root.header.flags);

    int tilesThisMap = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tilesThisMap < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("world/maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtripAdtSplit<versions::Shadowlands>(*fs, FileKey{adt}, af, adt);
      ++tilesThisMap;
      ++verified;
    }
  }
  CHECK(verified >= 3);
}
