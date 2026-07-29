#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <wowlib/db/tables/chr_races.hpp>
#include <wowlib/db/tables/manifest_interface_data.hpp>
#include <wowlib/db/tables/spell_name.hpp>
#include <wowlib/db/wire/wdc3.hpp>
#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/csv_listfile.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

namespace
{
  // The dbfilesclient/*.db2 paths, read straight from the listfile CSV (the
  // public CsvListfile API resolves single paths but does not enumerate).
  std::vector<std::string> db2_paths(const std::filesystem::path& csv)
  {
    std::vector<std::string> out;
    std::ifstream in{csv};
    std::string line;
    while (std::getline(in, line))
    {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
      const auto semi = line.find(';');
      if (semi == std::string::npos)
        continue;
      std::string path = line.substr(semi + 1);
      std::string lower = path;
      for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lower.starts_with("dbfilesclient/") && lower.ends_with(".db2"))
        out.push_back(lower);
    }
    return out;
  }
}

TEST_CASE("9.2.7: every DB2 in the corpus is a WDC3 that parses structurally",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  int wdc3 = 0, parsed = 0, other_magic = 0, encrypted_files = 0;
  std::vector<std::string> parse_failures;
  for (const std::string& path : db2_paths(listfile_csv))
  {
    const auto fdid = listfile->path_to_fdid(path);
    if (!fdid)
      continue;
    const auto data = storage->read_file(FileKey{*fdid});
    if (!data || data->size() < 4)
      continue;
    std::uint32_t magic = 0;
    std::memcpy(&magic, data->data(), 4);
    if (magic != db::wire::wdc3_magic)
    {
      ++other_magic;
      continue;
    }
    ++wdc3;
    const auto img = db::wire::Wdc3Image::parse(*data);
    if (!img)
    {
      if (parse_failures.size() < 20)
        parse_failures.push_back(path + ": " + img.error().message);
      continue;
    }
    ++parsed;
    if (std::ranges::any_of(img->sections, [](const auto& s) { return s.encrypted; }))
      ++encrypted_files;
  }

  INFO("parse failures:\n" << [&] {
    std::string s;
    for (const auto& f : parse_failures) s += f + '\n';
    return s;
  }());
  CHECK(other_magic == 0);          // the 9.2.7 corpus is 100% WDC3
  CHECK(wdc3 >= 800);               // ~835 locally readable
  CHECK(parsed == wdc3);            // every one parses
  CHECK(encrypted_files >= 100);    // ~138 carry encrypted sections
}

TEST_CASE("9.2.7: ManifestInterfaceData decodes with resolved strings",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  const auto fdid = listfile->path_to_fdid("dbfilesclient/manifestinterfacedata.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->read_file(FileKey{*fdid});
  REQUIRE(data.has_value());

  db::tables::ManifestInterfaceData<versions::shadowlands> manifest;
  REQUIRE(manifest.read(*data).has_value());
  CHECK(manifest.fully_decoded());
  CHECK(manifest.records.size() > 50'000);

  // id 21 is the intro logo — its path+name is stable and cross-checkable
  // against the listfile entry "21;interface/cinematics/logo_1024.avi".
  const auto logo = std::ranges::find_if(manifest.records,
                                         [](const auto& r) { return r.id == 21; });
  REQUIRE(logo != manifest.records.end());
  CHECK(logo->file_path == "Interface\\Cinematics\\");
  CHECK(logo->file_name == "Logo_1024.avi");
}

TEST_CASE("9.2.7: ChrRaces decodes fully (compression kinds, arrays)",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  const auto fdid = listfile->path_to_fdid("dbfilesclient/chrraces.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->read_file(FileKey{*fdid});
  REQUIRE(data.has_value());

  db::tables::ChrRaces<versions::shadowlands> races;
  REQUIRE(races.read(*data).has_value());
  CHECK(races.fully_decoded());
  // 9.2.7 ships well over 20 playable + internal races.
  CHECK(races.records.size() >= 20);
}

TEST_CASE("9.2.7: an encrypted table reports its sections and omits their rows",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  // spellname.db2 is encrypted in the WoWCircle repack (survey 2026-07-29).
  const auto fdid = listfile->path_to_fdid("dbfilesclient/spellname.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->read_file(FileKey{*fdid});
  REQUIRE(data.has_value());

  db::tables::SpellName<versions::shadowlands> spells;
  REQUIRE(spells.read(*data).has_value());  // read succeeds despite encryption
  if (!spells.encrypted_sections().empty())
  {
    CHECK_FALSE(spells.fully_decoded());
    CHECK(spells.encrypted_sections().front().key_hash != 0);
  }
}
