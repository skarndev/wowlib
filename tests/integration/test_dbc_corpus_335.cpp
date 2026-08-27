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
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mpqClient()),
                                  .version = versions::Wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());

  tests::CorpusStats stats;
#define x(Name) \
  tests::sweepTable<db::tables::Name<versions::Wotlk>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_WOTLK(x)
#undef x

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 150);  // 3.3.5a ships ~246 DBCs; most have wotlk defs
}

TEST_CASE("3.3.5a: Map.dbc spot checks against known truth", "[integration][db]")
{
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mpqClient()),
                                  .version = versions::Wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());

  const auto data = opened->readFile(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::Wotlk> map;
  REQUIRE(map.read(*data).has_value());

  const auto byId = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = byId(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");
  CHECK_FALSE(azeroth->map_name.at(Locale::enUS).empty());

  const auto kalimdor = byId(1);
  REQUIRE(kalimdor != map.records.end());
  CHECK(kalimdor->directory == "Kalimdor");

  const auto outland = byId(530);
  REQUIRE(outland != map.records.end());
  CHECK(outland->directory == "Expansion01");

  const auto northrend = byId(571);
  REQUIRE(northrend != map.records.end());
  CHECK(northrend->directory == "Northrend");
}
