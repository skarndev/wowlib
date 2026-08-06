#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <wowlib/db/tables/manifest_wotlk.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("3.3.5a: the full DBC corpus decodes and round-trips byte-perfectly",
          "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::mpq_client()),
                                  .version = versions::wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());

  tests::CorpusStats stats;
#define X(Name) \
  tests::sweep_table<db::tables::Name<versions::wotlk>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_WOTLK(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 150);  // 3.3.5a ships ~246 DBCs; most have wotlk defs
}

TEST_CASE("3.3.5a: Map.dbc spot checks against known truth", "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::mpq_client()),
                                  .version = versions::wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());

  const auto data = opened->read_file(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::wotlk> map;
  REQUIRE(map.read(*data).has_value());

  const auto by_id = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = by_id(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");
  CHECK_FALSE(azeroth->map_name.at(Locale::enUS).empty());

  const auto kalimdor = by_id(1);
  REQUIRE(kalimdor != map.records.end());
  CHECK(kalimdor->directory == "Kalimdor");

  const auto outland = by_id(530);
  REQUIRE(outland != map.records.end());
  CHECK(outland->directory == "Expansion01");

  const auto northrend = by_id(571);
  REQUIRE(northrend != map.records.end());
  CHECK(northrend->directory == "Northrend");
}
