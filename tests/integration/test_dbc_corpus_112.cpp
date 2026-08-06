#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <wowlib/db/tables/manifest_vanilla.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("1.12.2: the full DBC corpus decodes and round-trips byte-perfectly",
          "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::vanilla_client()),
                                  .version = versions::vanilla,
                                  .locale = tests::vanilla_locale()});
  REQUIRE(opened.has_value());

  tests::CorpusStats stats;
#define X(Name) \
  tests::sweep_table<db::tables::Name<versions::vanilla>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_VANILLA(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 60);
}

TEST_CASE("1.12.2: Map.dbc spot checks against known truth", "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::vanilla_client()),
                                  .version = versions::vanilla,
                                  .locale = tests::vanilla_locale()});
  REQUIRE(opened.has_value());

  const auto data = opened->read_file(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::vanilla> map;
  REQUIRE(map.read(*data).has_value());

  const auto by_id = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = by_id(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");
  // The local 1.12.2 install is a ruRU repack; localized text may sit in any
  // slot, so only the non-localized directory strings are asserted exactly.

  const auto kalimdor = by_id(1);
  REQUIRE(kalimdor != map.records.end());
  CHECK(kalimdor->directory == "Kalimdor");
}
