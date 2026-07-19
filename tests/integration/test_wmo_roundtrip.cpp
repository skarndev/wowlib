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
  void require_identical(const FileBuffer& original, const FileBuffer& rewritten,
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
        inside = fourcc_to_string(fourcc);
        break;
      }
      pos += 8 + size;
    }
    FAIL(std::format("{}: first divergence at {:#x} inside chunk {} (sizes {} vs {})", label,
                     at, inside, original.size(), rewritten.size()));
  }

  std::map<std::string, int> unknown_histogram;

  template <ClientVersion V>
  void tally_unknown(const chunk_extras& extras)
  {
    for (const unknown_chunk& u : extras.unknown)
      ++unknown_histogram[fourcc_to_string(u.fourcc)];
  }

  /** Round-trip one WMO: root and every group file, byte-for-byte. Returns the
      group count actually verified. */
  template <ClientVersion V>
  void roundtrip_wmo(fs::FileSystem& fs, const FileKey& root_key, const std::string& label)
  {
    const auto root_data = fs.read_file(root_key);
    REQUIRE(root_data.has_value());

    const auto root = formats::read<WmoRoot<V>>(*root_data);
    REQUIRE(root.has_value());
    CHECK(root->mver == wmo_version_v17);
    tally_unknown<V>(*root);

    const auto rewritten_root = formats::write(*root);
    REQUIRE(rewritten_root.has_value());
    require_identical(*root_data, *rewritten_root, label + " (root)");

    // group identity: GFID when present, name derivation otherwise
    const std::size_t n_groups = root->header.n_groups;
    const bool by_fdid = root->group_fdids.size() >= n_groups;
    std::string root_path;
    if (!by_fdid)
    {
      const FileKey resolved = fs.resolve(root_key);
      REQUIRE(resolved.path.has_value());
      root_path = *resolved.path;
      REQUIRE(root_path.ends_with(".wmo"));
    }

    for (std::size_t i = 0; i < n_groups; ++i)
    {
      const FileKey group_key =
        by_fdid ? FileKey{FileDataID{root->group_fdids[i]}}
                : FileKey{std::format("{}_{:03}.wmo",
                                      root_path.substr(0, root_path.size() - 4), i)};
      const auto group_data = fs.read_file(group_key);
      REQUIRE(group_data.has_value());

      const auto group = formats::read<WmoGroup<V>>(*group_data);
      REQUIRE(group.has_value());
      tally_unknown<V>(*group);
      tally_unknown<V>(group->body);

      const auto rewritten = formats::write(*group);
      REQUIRE(rewritten.has_value());
      require_identical(*group_data, *rewritten, std::format("{} (group {})", label, i));
    }

    // the assembled-entity path agrees
    const auto wmo = Wmo<V>::load(fs, root_key);
    REQUIRE(wmo.has_value());
    CHECK(wmo->groups.size() == n_groups);
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
}

TEST_CASE("3.3.5a WMOs rewrite byte-for-byte", "[integration][formats][wmo]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::mpq_client_name,
                                  .version = versions::wotlk});
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
    roundtrip_wmo<versions::wotlk>(*fs, FileKey{path}, path);
    ++verified;
  }
  dump_histogram("3.3.5a");
  CHECK(verified >= 5);  // enough coverage even if some curated paths drift
}

TEST_CASE("9.2.7 WMOs rewrite byte-for-byte", "[integration][formats][wmo]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile = tests::require_listfile();

  auto fs = fs::FileSystem::open({.client_path = clients / tests::casc_client_name,
                                  .version = versions::shadowlands,
                                  .listfile_csv = listfile});
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
      const auto stem_end = path.size() - 4;
      // "_123.wmo" group file?
      if (stem_end >= 4 && path[stem_end - 4] == '_' && isdigit(path[stem_end - 3])
          && isdigit(path[stem_end - 2]) && isdigit(path[stem_end - 1]))
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
    const auto probe = fs->read_file(key);
    if (!probe)
    {
      ++skipped;  // encrypted or absent from this install
      continue;
    }
    roundtrip_wmo<versions::shadowlands>(*fs, key, path);
    ++verified;
  }
  dump_histogram("9.2.7");
  WARN(std::format("9.2.7 sample: {} verified, {} unreadable/skipped", verified, skipped));
  CHECK(verified >= 15);
}
