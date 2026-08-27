#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <wowlib/db/tables/manifest_tbc.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("2.4.3: the full DBC corpus decodes and round-trips byte-perfectly",
          "[integration][db]")
{
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::tbcClient()),
                                  .version = versions::Tbc,
                                  .locale = tests::tbcLocale()});
  REQUIRE(opened.has_value());

  tests::CorpusStats stats;
#define X(Name) \
  tests::sweepTable<db::tables::Name<versions::Tbc>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_TBC(X)
#undef X

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 100);
}

TEST_CASE("2.4.3: Map.dbc spot checks against known truth", "[integration][db]")
{
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::tbcClient()),
                                  .version = versions::Tbc,
                                  .locale = tests::tbcLocale()});
  REQUIRE(opened.has_value());

  const auto data = opened->readFile(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::Tbc> map;
  REQUIRE(map.read(*data).has_value());

  const auto byId = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = byId(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");
  CHECK_FALSE(azeroth->map_name.at(tests::tbcLocale()).empty());

  const auto outland = byId(530);
  REQUIRE(outland != map.records.end());
  CHECK(outland->directory == "Expansion01");
}
