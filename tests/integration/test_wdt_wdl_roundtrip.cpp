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
  void require_identical(const FileBuffer& original, const FileBuffer& rewritten,
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
        inside = fourcc_to_string(fourcc);
        break;
      }
      pos += 8 + size;
    }
    FAIL(std::format("{}: first divergence at {:#x} inside chunk {} (sizes {} vs {})", label,
                     at, inside, original.size(), rewritten.size()));
  }

  std::map<std::string, int> unknown_histogram;

  void tally_unknown(const ChunkExtras& extras)
  {
    for (const UnknownChunk& u : extras.unknown)
      ++unknown_histogram[fourcc_to_string(u.fourcc)];
  }

  void dump_histogram(const char* which)
  {
    if (unknown_histogram.empty())
      return;
    std::string lines;
    for (const auto& [fourcc, count] : unknown_histogram)
      lines += std::format("  {} x{}\n", fourcc, count);
    WARN(std::format("{}: unmodeled chunks encountered (still round-tripped verbatim):\n{}",
                     which, lines));
    unknown_histogram.clear();
  }

  /** Byte-perfect cycle of one already-read chunked entity against its raw
      file bytes. */
  template <typename E>
  void require_roundtrip(const E& entity, const FileBuffer& raw, const std::string& label)
  {
    tally_unknown(entity);
    const auto rewritten = entity.write();
    REQUIRE(rewritten.has_value());
    require_identical(raw, *rewritten, label);
  }

  /** Round-trip a whole WDT: the main file byte-for-byte, the assembly read,
      and every satellite file the assembly located, byte-for-byte. */
  template <ClientVersion V>
  void roundtrip_wdt(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    const auto raw = fs.read_file(key);
    REQUIRE(raw.has_value());

    wdt::root::WDTRoot<V> root;
    REQUIRE(root.read(*raw).has_value());
    CHECK(root.mver == wdt::wdt_version_18);
    CHECK(root.tiles.size() == 64 * 64);
    require_roundtrip(root, *raw, label + " (main)");

    wdt::WDT<V> assembly;
    REQUIRE(assembly.read(fs, key).has_value());

    if constexpr (requires { assembly.occlusion; })
    {
      // locate each satellite's raw bytes the same way the assembly does and
      // compare the baked entity's rewrite against them
      const auto satellite_raw = [&](std::uint32_t fdid,
                                     std::string_view suffix) -> Result<FileBuffer> {
        if constexpr (requires { root.header.occ_fdid; })
        {
          if (fdid == 0)
            return FileBuffer{};
          if (!fs.exists(FileKey{FileDataID{fdid}}))
            return FileBuffer{};
          return fs.read_file(FileKey{FileDataID{fdid}});
        }
        else
        {
          const FileKey resolved = fs.resolve(key);
          REQUIRE(resolved.path.has_value());
          const std::string path =
            std::format("{}_{}.wdt", resolved.path->substr(0, resolved.path->size() - 4), suffix);
          if (!fs.exists(FileKey{path}))
            return FileBuffer{};
          return fs.read_file(FileKey{path});
        }
      };
      const auto check_satellite = [&](const auto& satellite, std::uint32_t fdid,
                                       std::string_view suffix) {
        const auto data = satellite_raw(fdid, suffix);
        REQUIRE(data.has_value());
        if (data->empty())
          return;
        require_roundtrip(satellite, *data, std::format("{} (_{})", label, suffix));
      };

      const auto fdid_of = [&](auto pick) -> std::uint32_t {
        if constexpr (requires { root.header.occ_fdid; })
          return pick(root.header);
        else
          return 0;
      };
      check_satellite(assembly.occlusion, fdid_of([](const auto& h) { return h.occ_fdid; }),
                      "occ");
      check_satellite(assembly.lights, fdid_of([](const auto& h) { return h.lgt_fdid; }), "lgt");
      if constexpr (requires { assembly.fogs; })
        check_satellite(assembly.fogs, fdid_of([](const auto& h) { return h.fogs_fdid; }),
                        "fogs");
      if constexpr (requires { assembly.particulates; })
        check_satellite(assembly.particulates, fdid_of([](const auto& h) { return h.mpv_fdid; }),
                        "mpv");
    }
  }

  /** Round-trip one WDL byte-for-byte and check the tile-table invariants the
      model builds on. */
  template <ClientVersion V>
  void roundtrip_wdl(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    const auto raw = fs.read_file(key);
    REQUIRE(raw.has_value());

    wdl::WDL<V> entity;
    REQUIRE(entity.read(*raw).has_value());
    CHECK(entity.mver == wdl::wdl_version_18);
    REQUIRE(entity.tile_offsets.size() == 64 * 64);

    // the pairing invariants the repeating-member model builds on
    std::size_t engaged = 0;
    for (const std::uint32_t offset : entity.tile_offsets)
      engaged += (offset != 0);
    CHECK(engaged == entity.heightmaps.size());
    if constexpr (requires { entity.holes; })
      CHECK(entity.holes.size() == entity.heightmaps.size());
    if constexpr (requires { entity.ocean_masks; })
      CHECK(entity.ocean_mask_tiles().size() == entity.ocean_masks.size());

    require_roundtrip(entity, *raw, label);
  }
}

TEST_CASE("3.3.5a WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::mpq_client_name,
                                  .version = versions::wotlk});
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
    const std::string wdt_path = std::format("World/Maps/{0}/{0}.wdt", map);
    const std::string wdl_path = std::format("World/Maps/{0}/{0}.wdl", map);
    if (!fs->exists(wdt_path))
    {
      WARN("not in client, skipped: " + wdt_path);
      continue;
    }
    roundtrip_wdt<versions::wotlk>(*fs, FileKey{wdt_path}, map);
    if (fs->exists(wdl_path))
      roundtrip_wdl<versions::wotlk>(*fs, FileKey{wdl_path}, map + " (wdl)");
    ++verified;
  }
  dump_histogram("3.3.5a");
  CHECK(verified >= 6);
}

TEST_CASE("1.12.2 WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::vanilla_client_name,
                                  .version = versions::vanilla,
                                  .locale = tests::vanilla_locale});
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
    const std::string wdt_path = std::format("World/Maps/{0}/{0}.wdt", map);
    const std::string wdl_path = std::format("World/Maps/{0}/{0}.wdl", map);
    if (!fs->exists(wdt_path))
    {
      WARN("not in client, skipped: " + wdt_path);
      continue;
    }
    roundtrip_wdt<versions::vanilla>(*fs, FileKey{wdt_path}, map);
    if (fs->exists(wdl_path))
      roundtrip_wdl<versions::vanilla>(*fs, FileKey{wdl_path}, map + " (wdl)");
    ++verified;
  }
  dump_histogram("1.12.2");
  CHECK(verified >= 3);
}

TEST_CASE("2.4.3 WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::tbc_client_name,
                                  .version = versions::tbc,
                                  .locale = tests::tbc_locale});
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
    const std::string wdt_path = std::format("World/Maps/{0}/{0}.wdt", map);
    const std::string wdl_path = std::format("World/Maps/{0}/{0}.wdl", map);
    if (!fs->exists(wdt_path))
    {
      WARN("not in client, skipped: " + wdt_path);
      continue;
    }
    roundtrip_wdt<versions::tbc>(*fs, FileKey{wdt_path}, map);
    if (fs->exists(wdl_path))
      roundtrip_wdl<versions::tbc>(*fs, FileKey{wdl_path}, map + " (wdl)");
    ++verified;
  }
  dump_histogram("2.4.3");
  CHECK(verified >= 3);
}

TEST_CASE("9.2.7 WDTs and WDLs rewrite byte-for-byte", "[integration][formats][wdt][wdl]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile = tests::require_listfile();

  auto fs = fs::FileSystem::open({.client_path = clients / tests::casc_client_name,
                                  .version = versions::shadowlands,
                                  .listfile_csv = listfile});
  REQUIRE(fs.has_value());

  // sample MAIN .wdt files (not the satellites) and .wdl files from the
  // community listfile
  std::vector<std::pair<std::uint32_t, std::string>> wdts;
  std::vector<std::pair<std::uint32_t, std::string>> wdls;
  {
    std::ifstream in{listfile};
    REQUIRE(in.good());
    const auto is_satellite = [](const std::string& path) {
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
      if (path.ends_with(".wdt") && !is_satellite(path))
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

  int wdt_verified = 0;
  int skipped = 0;
  for (const auto& [fdid, path] : wdts)
  {
    if (wdt_verified >= 20)
      break;
    const FileKey key{path, FileDataID{fdid}};
    if (!fs->exists(key) || !fs->read_file(key))
    {
      ++skipped;  // encrypted or absent from this install
      continue;
    }
    roundtrip_wdt<versions::shadowlands>(*fs, key, path);
    ++wdt_verified;
  }

  int wdl_verified = 0;
  for (const auto& [fdid, path] : wdls)
  {
    if (wdl_verified >= 20)
      break;
    const FileKey key{path, FileDataID{fdid}};
    if (!fs->exists(key) || !fs->read_file(key))
    {
      ++skipped;
      continue;
    }
    roundtrip_wdl<versions::shadowlands>(*fs, key, path);
    ++wdl_verified;
  }

  dump_histogram("9.2.7");
  WARN(std::format("9.2.7 sample: {} WDTs + {} WDLs verified, {} unreadable/skipped",
                   wdt_verified, wdl_verified, skipped));
  CHECK(wdt_verified >= 15);
  CHECK(wdl_verified >= 15);
}
