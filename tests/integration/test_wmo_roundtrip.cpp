#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <wowlib/formats/wmo/wmo.hpp>
#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::wmo;
namespace fsys = std::filesystem;

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

    // locate the chunk of the original stream the divergence falls into
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

  std::map<std::string, int> gUnknownHistogram;

  void tallyUnknown(const ChunkExtras& extras)
  {
    for (const UnknownChunk& u : extras.unknown)
      ++gUnknownHistogram[fourccToString(u.fourcc)];
  }

  /** Round-trip one WMO: root and every group file, byte-for-byte. Returns the
      group count actually verified. */
  template <ClientVersion V>
  void roundtripWmo(fs::FileSystem& fs, const FileKey& rootKey, const std::string& label)
  {
    const auto rootData = fs.readFile(rootKey);
    REQUIRE(rootData.has_value());

    WMORoot<V> root;
    REQUIRE(root.read(*rootData).has_value());
    CHECK(root.mver == WmoVersionV17);
    tallyUnknown(root);

    const auto rewrittenRoot = root.write();
    REQUIRE(rewrittenRoot.has_value());
    requireIdentical(*rootData, *rewrittenRoot, label + " (root)");

    // group identity: GFID when present (Legion+), name derivation otherwise. The
    // GFID member only exists on versions that have it, so guard the access.
    const std::size_t nGroups = root.header.nGroups;
    bool byFdid = false;
    if constexpr (requires { root.groupFdids; })
      byFdid = root.groupFdids.size() >= nGroups;
    std::string rootPath;
    if (!byFdid)
    {
      const FileKey resolved = fs.resolve(rootKey);
      REQUIRE(resolved.path.has_value());
      rootPath = *resolved.path;
      REQUIRE(rootPath.ends_with(".wmo"));
    }

    for (std::size_t i = 0; i < nGroups; ++i)
    {
      const FileKey groupKey = [&]() -> FileKey {
        if constexpr (requires { root.groupFdids; })
          if (byFdid)
            return FileKey{FileDataID{root.groupFdids[i]}};
        return FileKey{std::format("{}_{:03}.wmo",
                                   rootPath.substr(0, rootPath.size() - 4), i)};
      }();
      const auto groupData = fs.readFile(groupKey);
      REQUIRE(groupData.has_value());

      WMOGroup<V> group;
      REQUIRE(group.read(*groupData).has_value());
      tallyUnknown(group);
      tallyUnknown(group.body);

      const auto rewritten = group.write();
      REQUIRE(rewritten.has_value());
      requireIdentical(*groupData, *rewritten, std::format("{} (group {})", label, i));
    }

    // the assembled-entity path agrees
    WMO<V> wmo;
    REQUIRE(wmo.read(fs, rootKey).has_value());
    CHECK(wmo.groups.size() == nGroups);

    // a freshly read, unmodified client file passes validation with zero
    // errors (warnings are allowed - they mark states real files ship)
    if (const auto valid = wmo.ensureValid(); !valid)
      FAIL(std::format("{}: {}", label, valid.error().message));
  }

  void dumpHistogram(const char* which)
  {
    if (gUnknownHistogram.empty())
      return;
    std::string lines;
    for (const auto& [fourcc, count] : gUnknownHistogram)
      lines += std::format("  {} x{}\n", fourcc, count);
    WARN(std::format("{}: unmodeled chunks encountered (still round-tripped verbatim):\n{}",
                     which, lines));
    gUnknownHistogram.clear();
  }
}

TEST_CASE("3.3.5a WMOs rewrite byte-for-byte", "[integration][formats][wmo]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::mpqClient(),
                                  .version = versions::Wotlk});
  REQUIRE(fs.has_value());

  // curated spread: tiny to huge, dungeons to capitals; entries missing from
  // the client are skipped so path spelling never breaks the suite
  const std::vector<std::string> candidates{
    "World/wmo/Dungeon/AZ_Subway/Subway.wmo",
    "World/wmo/Azeroth/Buildings/Stormwind/Stormwind.wmo",
    "World/wmo/Azeroth/Buildings/GoldshireInn/GoldshireInn.wmo",
    "World/wmo/Azeroth/Buildings/Human_Farm/Farm.wmo",
    "World/wmo/KhazModan/Cities/Ironforge/Ironforge.wmo",
    "World/wmo/Northrend/Dalaran/ND_Dalaran.wmo",
    "World/wmo/Northrend/Buildings/IceCrown/ND_IcecrownCitadel/ND_IcecrownCitadel.wmo",
    "World/wmo/Kalimdor/Ogrimmar/Ogrimmar.wmo",
    "World/wmo/Dungeon/MD_Crypt/MD_Crypt_A.wmo",
    "World/wmo/Azeroth/Buildings/GriffonAviary/GriffonAviary.wmo",
  };

  int verified = 0;
  for (const auto& path : candidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client, skipped: " + path);
      continue;
    }
    roundtripWmo<versions::Wotlk>(*fs, FileKey{path}, path);
    ++verified;
  }
  dumpHistogram("3.3.5a");
  CHECK(verified >= 5);  // enough coverage even if some curated paths drift
}

TEST_CASE("1.12.2 WMOs rewrite byte-for-byte", "[integration][formats][wmo]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::vanillaClient(),
                                  .version = versions::Vanilla,
                                  .locale = tests::vanillaLocale()});
  REQUIRE(fs.has_value());

  // vanilla-era spread: capitals, inns, farms, a dungeon; entries missing from
  // the client are skipped so path spelling never breaks the suite
  const std::vector<std::string> candidates{
    "World/wmo/Dungeon/AZ_Subway/Subway.wmo",
    "World/wmo/Azeroth/Buildings/Stormwind/Stormwind.wmo",
    "World/wmo/Azeroth/Buildings/GoldshireInn/GoldshireInn.wmo",
    "World/wmo/Azeroth/Buildings/Human_Farm/Farm.wmo",
    "World/wmo/KhazModan/Cities/Ironforge/Ironforge.wmo",
    "World/wmo/Kalimdor/Ogrimmar/Ogrimmar.wmo",
    "World/wmo/Dungeon/MD_Crypt/MD_Crypt_A.wmo",
    "World/wmo/Azeroth/Buildings/GriffonAviary/GriffonAviary.wmo",
  };

  int verified = 0;
  for (const auto& path : candidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client, skipped: " + path);
      continue;
    }
    roundtripWmo<versions::Vanilla>(*fs, FileKey{path}, path);
    ++verified;
  }
  dumpHistogram("1.12.2");
  CHECK(verified >= 4);
}

TEST_CASE("2.4.3 WMOs rewrite byte-for-byte", "[integration][formats][wmo]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::tbcClient(),
                                  .version = versions::Tbc,
                                  .locale = tests::tbcLocale()});
  REQUIRE(fs.has_value());

  // vanilla capitals/dungeons plus TBC's Outland structures; entries missing
  // from the client are skipped so path spelling never breaks the suite
  const std::vector<std::string> candidates{
    "World/wmo/Azeroth/Buildings/Stormwind/Stormwind.wmo",
    "World/wmo/Azeroth/Buildings/GoldshireInn/GoldshireInn.wmo",
    "World/wmo/Azeroth/Buildings/Human_Farm/Farm.wmo",
    "World/wmo/KhazModan/Cities/Ironforge/Ironforge.wmo",
    "World/wmo/Kalimdor/Ogrimmar/Ogrimmar.wmo",
    "World/wmo/Draenor/Shattrath/Shattrath.wmo",
    "World/wmo/Outland/Dungeon/HF_Ramparts/HF_Ramparts.wmo",
    "World/wmo/Dungeon/AZ_Subway/Subway.wmo",
  };

  int verified = 0;
  for (const auto& path : candidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client, skipped: " + path);
      continue;
    }
    roundtripWmo<versions::Tbc>(*fs, FileKey{path}, path);
    ++verified;
  }
  dumpHistogram("2.4.3");
  CHECK(verified >= 4);
}

TEST_CASE("9.2.7 WMOs rewrite byte-for-byte", "[integration][formats][wmo]")
{
  const auto listfile = tests::requireListfile();

  auto fs = fs::FileSystem::open({.clientPath = tests::cascClient(),
                                  .version = versions::Shadowlands,
                                  .listfileCsv = listfile});
  REQUIRE(fs.has_value());

  // sample root WMOs from the community listfile: *.wmo that are neither
  // group files (_NNN) nor LOD variants
  std::vector<std::pair<std::uint32_t, std::string>> roots;
  {
    std::ifstream in{listfile};
    REQUIRE(in.good());
    std::string line;
    while (std::getline(in, line))
    {
      if (!line.empty() && line.back() == '\r')  // community listfile is CRLF
        line.pop_back();
      const auto sep = line.find(';');
      if (sep == std::string::npos)
        continue;
      std::string path = line.substr(sep + 1);
      if (!path.ends_with(".wmo"))
        continue;
      const auto stemEnd = path.size() - 4;
      // "_123.wmo" group file?
      if (stemEnd >= 4 && path[stemEnd - 4] == '_' && isdigit(path[stemEnd - 3])
          && isdigit(path[stemEnd - 2]) && isdigit(path[stemEnd - 1]))
        continue;
      if (path.find("_lod") != std::string::npos)
        continue;
      roots.emplace_back(static_cast<std::uint32_t>(std::stoul(line.substr(0, sep))),
                         std::move(path));
    }
  }
  REQUIRE(roots.size() > 100);

  std::mt19937 rng{20260719};  // fixed seed: reproducible sample
  std::shuffle(roots.begin(), roots.end(), rng);

  int verified = 0;
  int skipped = 0;
  for (const auto& [fdid, path] : roots)
  {
    if (verified >= 25)
      break;
    const FileKey key{path, FileDataID{fdid}};
    const auto probe = fs->readFile(key);
    if (!probe)
    {
      ++skipped;  // encrypted or absent from this install
      continue;
    }
    roundtripWmo<versions::Shadowlands>(*fs, key, path);
    ++verified;
  }
  dumpHistogram("9.2.7");
  WARN(std::format("9.2.7 sample: {} verified, {} unreadable/skipped", verified, skipped));
  CHECK(verified >= 15);
}
