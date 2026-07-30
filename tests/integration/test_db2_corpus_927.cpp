#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <wowlib/db/tables/chr_races.hpp>
#include <wowlib/db/tables/creature_display_info_extra.hpp>
#include <wowlib/db/tables/creature_model_data.hpp>
#include <wowlib/db/tables/location.hpp>
#include <wowlib/db/tables/manifest_interface_data.hpp>
#include <wowlib/db/tables/sound_kit.hpp>
#include <wowlib/db/tables/spell.hpp>
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

TEST_CASE("9.2.7: Spell (sparse/offset-map) decodes inline strings by id",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  const auto fdid = listfile->path_to_fdid("dbfilesclient/spell.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->read_file(FileKey{*fdid});
  REQUIRE(data.has_value());

  // spell.db2 is sparse (flag 0x05) with 40 encrypted sections + one large
  // unencrypted section of inline null-terminated description strings.
  db::tables::Spell<versions::shadowlands> spells;
  REQUIRE(spells.read(*data).has_value());
  CHECK(spells.records.size() > 50'000);
  CHECK_FALSE(spells.encrypted_sections().empty());

  const auto by_id = [&](std::uint32_t id) {
    return std::ranges::find_if(spells.records, [&](const auto& r) {
      return static_cast<std::uint32_t>(r.id) == id;
    });
  };
  const auto instakill = by_id(5);
  REQUIRE(instakill != spells.records.end());
  CHECK(instakill->description.starts_with("Instantly Kills the target."));
  // Spell 133 is Fireball — its description is stable across builds.
  const auto fireball = by_id(133);
  REQUIRE(fireball != spells.records.end());
  CHECK(fireball->description.find("fiery ball") != std::string::npos);
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
    // An encrypted table can't be re-encoded (its encrypted records share the
    // file layout) — write() re-emits the original image VERBATIM so the
    // encrypted sections stay intact.
    const auto written = spells.write();
    REQUIRE(written.has_value());
    REQUIRE(written->size() == data->size());
    CHECK(std::memcmp(written->data(), data->data(), data->size()) == 0);
  }
}

TEST_CASE("9.2.7: key-flagged sections that decrypted are decoded, not skipped",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  // location.db2 has two sections that carry a tact_key_hash but whose records
  // arrived non-zero (the storage held the key). A section is only undecodable
  // when its records are zero-filled, so these decode normally — skipping on the
  // key flag alone would have dropped ~100k rows.
  const auto fdid = listfile->path_to_fdid("dbfilesclient/location.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->read_file(FileKey{*fdid});
  REQUIRE(data.has_value());

  db::tables::Location<versions::shadowlands> location;
  REQUIRE(location.read(*data).has_value());
  CHECK(location.fully_decoded());
  CHECK(location.encrypted_sections().empty());
  CHECK(location.records.size() > 100'000);
}

TEST_CASE("9.2.7: TACT key registration is accepted by the CASC storage",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  // A well-formed key registers; the community list format imports cleanly.
  const std::array<std::byte, 16> key{};
  CHECK(storage->add_encryption_key(0xFA505078126ACB3EULL, key).has_value());
  CHECK(storage->import_keys(
              "FA505078126ACB3E BDC51862ABED79B2A3A4EF1B3556EBD3\n").has_value());
}

TEST_CASE("9.2.7: a keyless table preserves verbatim, or drops to plaintext",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  // soundkit.db2 has keyless (undecryptable, zero-filled) sections in this
  // repack, so some rows are missing from the decode.
  const auto fdid = listfile->path_to_fdid("dbfilesclient/soundkit.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->read_file(FileKey{*fdid});
  REQUIRE(data.has_value());

  db::tables::SoundKit<versions::shadowlands> sound;
  REQUIRE(sound.read(*data).has_value());
  if (sound.fully_decoded())
    return;  // this repack happened to have the keys — nothing to demonstrate
  const std::size_t decoded_rows = sound.records.size();

  // Preserve (the default): the original image, byte for byte.
  const auto preserved = sound.write(db::EncryptedPolicy::Preserve);
  REQUIRE(preserved.has_value());
  REQUIRE(preserved->size() == data->size());
  CHECK(std::memcmp(preserved->data(), data->data(), data->size()) == 0);

  // Drop: a plain WDC3 of just the decoded rows — no key needed to load it.
  const auto dropped = sound.write(db::EncryptedPolicy::Drop);
  REQUIRE(dropped.has_value());
  db::tables::SoundKit<versions::shadowlands> reread;
  REQUIRE(reread.read(*dropped).has_value());
  CHECK(reread.fully_decoded());                 // the rewrite carries no encryption
  CHECK(reread.encrypted_sections().empty());
  CHECK(reread.records.size() == decoded_rows);  // exactly the rows we had
}

TEST_CASE("9.2.7: WDC3 write is a semantic round-trip (decode == re-decode)",
          "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = clients / tests::casc_client_name,
                                    .build = 45745});
  REQUIRE(storage.has_value());

  // A WDC3 write is a canonical re-encode (not byte-identical); the guarantee
  // is that re-reading it yields the same SET of records by id (a write coalesces
  // duplicate rows into copy entries, which can reorder a multi-section table).
  // The write reuses each column's original compression scheme, so an equal
  // re-decode also proves the pallet / common / bitpacked-signed / array
  // encoders round-trip. `max_pct` guards that the re-encode stays near
  // Blizzard's size (compression reproduced, not exploded).
  const auto check = [&](std::string_view path, auto table, int max_pct) {
    const auto fdid = listfile->path_to_fdid(path);
    REQUIRE(fdid.has_value());
    const auto data = storage->read_file(FileKey{*fdid});
    REQUIRE(data.has_value());

    REQUIRE(table.read(*data).has_value());
    REQUIRE(table.fully_decoded());
    const auto written = table.write();
    REQUIRE(written.has_value());
    CHECK(std::memcmp(written->data(), "WDC3", 4) == 0);
    CHECK(written->size() * 100 <= data->size() * max_pct);

    decltype(table) reread;
    REQUIRE(reread.read(*written).has_value());
    REQUIRE(reread.records.size() == table.records.size());
    const auto by_id = [](const auto& a, const auto& b) { return a.id < b.id; };
    std::ranges::sort(table.records, by_id);
    std::ranges::sort(reread.records, by_id);
    CHECK(reread.records == table.records);
  };

  check("dbfilesclient/manifestinterfacedata.db2",
        db::tables::ManifestInterfaceData<versions::shadowlands>{}, 105);
  check("dbfilesclient/chrraces.db2", db::tables::ChrRaces<versions::shadowlands>{}, 105);
  // CreatureModelData is pallet-heavy (21 pallet + 1 pallet-array of floats);
  // reproducing the pallet keeps it at ~Blizzard size instead of ~2x.
  check("dbfilesclient/creaturemodeldata.db2",
        db::tables::CreatureModelData<versions::shadowlands>{}, 110);
  // CreatureDisplayInfoExtra exercises common-data compression and an INLINE id
  // (3 sections); it must decode-equal by id and stay near Blizzard's size.
  check("dbfilesclient/creaturedisplayinfoextra.db2",
        db::tables::CreatureDisplayInfoExtra<versions::shadowlands>{}, 105);
}
