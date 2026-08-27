#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <wowlib/formats/wdl/wdl.hpp>
#include <wowlib/formats/wdt/wdt.hpp>
#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::formats;

namespace
{
  /** Fail with the first divergence offset and the enclosing chunk — the
      byte-perfect guarantee's debugging lens. */
  void requireIdentical(const FileBuffer& original, const FileBuffer& rewritten,
                         const std::string& label)
  {
    if (original == rewritten)
      return;

    const std::size_t common = std::min(original.size(), rewritten.size());
    std::size_t at = 0;
    while (at < common && original[at] == rewritten[at])
      ++at;

    std::string inside = "<no chunk>";
    for (std::size_t pos = 0; pos + 8 <= original.size();)
    {
      std::uint32_t fourcc = 0;
      std::uint32_t size = 0;
      std::memcpy(&fourcc, original.data() + pos, 4);
      std::memcpy(&size, original.data() + pos + 4, 4);
      if (at < pos + 8 + size)
      {
        inside = fourccToString(fourcc);
        break;
      }
      pos += 8 + size;
    }
    FAIL(std::format("{}: first divergence at {:#x} inside chunk {} (sizes {} vs {})", label,
                     at, inside, original.size(), rewritten.size()));
  }

  // Kept separate so a WDL-only chunk (e.g. the WotLK+ MAHO hole masks) is never
  // mistaken for a WDT chunk — the two files share this test case but not their
  // chunk vocabularies.
  std::map<std::string, int> gUnknownHistogramWdt;
  std::map<std::string, int> gUnknownHistogramWdl;

  void tallyUnknown(const ChunkExtras& extras, std::map<std::string, int>& hist)
  {
    for (const UnknownChunk& u : extras.unknown)
      ++hist[fourccToString(u.fourcc)];
  }

  void dumpOne(const char* which, const char* kind, std::map<std::string, int>& hist)
  {
    if (hist.empty())
      return;
    std::string lines;
    for (const auto& [fourcc, count] : hist)
      lines += std::format("  {} x{}\n", fourcc, count);
    WARN(std::format("{} {}: unmodeled chunks encountered (still round-tripped verbatim):\n{}",
                     which, kind, lines));
    hist.clear();
  }

  void dumpHistogram(const char* which)
  {
    dumpOne(which, "WDT", gUnknownHistogramWdt);
    dumpOne(which, "WDL", gUnknownHistogramWdl);
  }

  /** Byte-perfect cycle of one already-read chunked entity against its raw
      file bytes; unmodeled chunks are tallied into @a hist (the WDT or WDL one). */
  template <typename E>
  void requireRoundtrip(const E& entity, const FileBuffer& raw, const std::string& label,
                         std::map<std::string, int>& hist)
  {
    tallyUnknown(entity, hist);
    const auto rewritten = entity.write();
    REQUIRE(rewritten.has_value());
    requireIdentical(raw, *rewritten, label);
  }

  /** Round-trip a whole WDT: the main file byte-for-byte, the assembly read,
      and every satellite file the assembly located, byte-for-byte. */
  template <ClientVersion V>
  void roundtripWdt(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    const auto raw = fs.readFile(key);
    REQUIRE(raw.has_value());

    wdt::root::WDTRoot<V> root;
    REQUIRE(root.read(*raw).has_value());
    CHECK(root.mver == wdt::WdtVersion18);
    CHECK(root.tiles.size() == 64 * 64);
    // a freshly read, unmodified client file passes validation with zero
    // errors (warnings are allowed - they mark states real files ship)
    if (const auto valid = root.ensureValid(); !valid)
      FAIL(std::format("{} (main): {}", label, valid.error().message));
    requireRoundtrip(root, *raw, label + " (main)", gUnknownHistogramWdt);

    wdt::WDT<V> assembly;
    REQUIRE(assembly.read(fs, key).has_value());

    if constexpr (requires { assembly.occlusion; })
    {
      // locate each satellite's raw bytes the same way the assembly does and
      // compare the baked entity's rewrite against them
      const auto satelliteRaw = [&](std::uint32_t fdid,
                                     std::string_view suffix) -> Result<FileBuffer> {
        if constexpr (requires { root.header.occFdid; })
        {
          if (fdid == 0)
            return FileBuffer{};
          if (!fs.exists(FileKey{FileDataID{fdid}}))
            return FileBuffer{};
          return fs.readFile(FileKey{FileDataID{fdid}});
        }
        else
        {
          const FileKey resolved = fs.resolve(key);
          REQUIRE(resolved.path.has_value());
          const std::string path =
            std::format("{}_{}.wdt", resolved.path->substr(0, resolved.path->size() - 4), suffix);
          if (!fs.exists(FileKey{path}))
            return FileBuffer{};
          return fs.readFile(FileKey{path});
        }
      };
      const auto checkSatellite = [&](const auto& satellite, std::uint32_t fdid,
                                       std::string_view suffix) {
        const auto data = satelliteRaw(fdid, suffix);
        REQUIRE(data.has_value());
        if (data->empty())
          return;
        requireRoundtrip(satellite, *data, std::format("{} (_{})", label, suffix),
                          gUnknownHistogramWdt);
      };

      const auto fdidOf = [&](auto pick) -> std::uint32_t {
        if constexpr (requires { root.header.occFdid; })
          return pick(root.header);
        else
          return 0;
      };
      checkSatellite(assembly.occlusion, fdidOf([](const auto& h) { return h.occFdid; }),
                      "occ");
      checkSatellite(assembly.lights, fdidOf([](const auto& h) { return h.lgtFdid; }), "lgt");
      if constexpr (requires { assembly.fogs; })
        checkSatellite(assembly.fogs, fdidOf([](const auto& h) { return h.fogsFdid; }),
                        "fogs");
      if constexpr (requires { assembly.particulates; })
        checkSatellite(assembly.particulates, fdidOf([](const auto& h) { return h.mpvFdid; }),
                        "mpv");
    }
  }

  /** Round-trip one WDL byte-for-byte and check the tile-table invariants the
      model builds on. */
  template <ClientVersion V>
  void roundtripWdl(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    const auto raw = fs.readFile(key);
    REQUIRE(raw.has_value());

    wdl::WDL<V> entity;
    REQUIRE(entity.read(*raw).has_value());
    CHECK(entity.mver == wdl::WdlVersion18);
    REQUIRE(entity.tileOffsets.size() == 64 * 64);
    if (const auto valid = entity.ensureValid(); !valid)
      FAIL(std::format("{}: {}", label, valid.error().message));

    // the pairing invariants the repeating-member model builds on
    std::size_t engaged = 0;
    for (const std::uint32_t offset : entity.tileOffsets)
      engaged += (offset != 0);
    CHECK(engaged == entity.heightmaps.size());
    if constexpr (requires { entity.holes; })
      CHECK(entity.holes.size() == entity.heightmaps.size());
    if constexpr (requires { entity.oceanMasks; })
      CHECK(entity.oceanMaskTiles().size() == entity.oceanMasks.size());

    requireRoundtrip(entity, *raw, label, gUnknownHistogramWdl);
  }
}

TEST_CASE("3.3.5a WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::mpqClient(),
                                  .version = versions::Wotlk});
  REQUIRE(fs.has_value());

  // curated spread: continents, battlegrounds, WMO-only instances; entries
  // missing from the client are skipped so name spelling never breaks the suite
  const std::vector<std::string> maps{
    "Azeroth",           "Kalimdor",       "Expansion01",     "Northrend",
    "PVPZone01",         "PVPZone04",      "DeadminesInstance", "Shadowfang",
    "StormwindJail",     "Ulduar",         "IcecrownCitadel", "ScarletMonastery",
  };

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdtPath = std::format("World/Maps/{0}/{0}.wdt", map);
    const std::string wdlPath = std::format("World/Maps/{0}/{0}.wdl", map);
    if (!fs->exists(wdtPath))
    {
      WARN("not in client, skipped: " + wdtPath);
      continue;
    }
    roundtripWdt<versions::Wotlk>(*fs, FileKey{wdtPath}, map);
    if (fs->exists(wdlPath))
      roundtripWdl<versions::Wotlk>(*fs, FileKey{wdlPath}, map + " (wdl)");
    ++verified;
  }
  dumpHistogram("3.3.5a");
  CHECK(verified >= 6);
}

TEST_CASE("1.12.2 WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::vanillaClient(),
                                  .version = versions::Vanilla,
                                  .locale = tests::vanillaLocale()});
  REQUIRE(fs.has_value());

  // vanilla continents, battlegrounds and WMO-only instances (no TBC/WotLK
  // maps); entries missing from the client are skipped so name spelling never
  // breaks the suite
  const std::vector<std::string> maps{
    "Azeroth",         "Kalimdor",         "PVPZone01",    "DeadminesInstance",
    "Shadowfang",      "StormwindJail",    "ScarletMonastery",
  };

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdtPath = std::format("World/Maps/{0}/{0}.wdt", map);
    const std::string wdlPath = std::format("World/Maps/{0}/{0}.wdl", map);
    if (!fs->exists(wdtPath))
    {
      WARN("not in client, skipped: " + wdtPath);
      continue;
    }
    roundtripWdt<versions::Vanilla>(*fs, FileKey{wdtPath}, map);
    if (fs->exists(wdlPath))
      roundtripWdl<versions::Vanilla>(*fs, FileKey{wdlPath}, map + " (wdl)");
    ++verified;
  }
  dumpHistogram("1.12.2");
  CHECK(verified >= 3);
}

TEST_CASE("2.4.3 WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::tbcClient(),
                                  .version = versions::Tbc,
                                  .locale = tests::tbcLocale()});
  REQUIRE(fs.has_value());

  // vanilla continents + Outland (Expansion01) + TBC instances; entries missing
  // from the client are skipped so name spelling never breaks the suite
  const std::vector<std::string> maps{
    "Azeroth",          "Kalimdor",       "Expansion01",    "PVPZone01",
    "HellfireMilitary", "ShadowfangKeep", "Shadowfang",
  };

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdtPath = std::format("World/Maps/{0}/{0}.wdt", map);
    const std::string wdlPath = std::format("World/Maps/{0}/{0}.wdl", map);
    if (!fs->exists(wdtPath))
    {
      WARN("not in client, skipped: " + wdtPath);
      continue;
    }
    roundtripWdt<versions::Tbc>(*fs, FileKey{wdtPath}, map);
    if (fs->exists(wdlPath))
      roundtripWdl<versions::Tbc>(*fs, FileKey{wdlPath}, map + " (wdl)");
    ++verified;
  }
  dumpHistogram("2.4.3");
  CHECK(verified >= 3);
}

TEST_CASE("9.2.7 WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  const auto listfile = tests::requireListfile();

  auto fs = fs::FileSystem::open({.clientPath = tests::cascClient(),
                                  .version = versions::Shadowlands,
                                  .listfileCsv = listfile});
  REQUIRE(fs.has_value());

  // sample MAIN .wdt files (not the satellites) and .wdl files from the
  // community listfile
  std::vector<std::pair<std::uint32_t, std::string>> wdts;
  std::vector<std::pair<std::uint32_t, std::string>> wdls;
  {
    std::ifstream in{listfile};
    REQUIRE(in.good());
    const auto isSatellite = [](const std::string& path) {
      const std::string stem = path.substr(0, path.size() - 4);
      for (const char* suffix : {"_occ", "_lgt", "_fogs", "_mpv", "_tex", "_wmo", "_psd", "_pd4"})
        if (stem.ends_with(suffix))
          return true;
      return false;
    };
    std::string line;
    while (std::getline(in, line))
    {
      if (!line.empty() && line.back() == '\r')  // community listfile is CRLF
        line.pop_back();
      const auto sep = line.find(';');
      if (sep == std::string::npos)
        continue;
      std::string path = line.substr(sep + 1);
      const auto fdid = static_cast<std::uint32_t>(std::stoul(line.substr(0, sep)));
      if (path.ends_with(".wdt") && !isSatellite(path))
        wdts.emplace_back(fdid, std::move(path));
      else if (path.ends_with(".wdl"))
        wdls.emplace_back(fdid, std::move(path));
    }
  }
  REQUIRE(wdts.size() > 100);
  REQUIRE(wdls.size() > 100);

  std::mt19937 rng{20260726};  // fixed seed: reproducible sample
  std::shuffle(wdts.begin(), wdts.end(), rng);
  std::shuffle(wdls.begin(), wdls.end(), rng);

  int wdtVerified = 0;
  int skipped = 0;
  for (const auto& [fdid, path] : wdts)
  {
    if (wdtVerified >= 20)
      break;
    const FileKey key{path, FileDataID{fdid}};
    if (!fs->exists(key) || !fs->readFile(key))
    {
      ++skipped;  // encrypted or absent from this install
      continue;
    }
    roundtripWdt<versions::Shadowlands>(*fs, key, path);
    ++wdtVerified;
  }

  int wdlVerified = 0;
  for (const auto& [fdid, path] : wdls)
  {
    if (wdlVerified >= 20)
      break;
    const FileKey key{path, FileDataID{fdid}};
    if (!fs->exists(key) || !fs->readFile(key))
    {
      ++skipped;
      continue;
    }
    roundtripWdl<versions::Shadowlands>(*fs, key, path);
    ++wdlVerified;
  }

  dumpHistogram("9.2.7");
  WARN(std::format("9.2.7 sample: {} WDTs + {} WDLs verified, {} unreadable/skipped",
                   wdtVerified, wdlVerified, skipped));
  CHECK(wdtVerified >= 15);
  CHECK(wdlVerified >= 15);
}
