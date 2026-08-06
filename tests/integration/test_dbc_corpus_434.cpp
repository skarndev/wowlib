#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <wowlib/db/tables/item.hpp>
#include <wowlib/db/tables/manifest_cata.hpp>
#include <wowlib/db/tables/map.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 4.3.4 is the first UpdateChain client: DBFilesClient lives in
// locale-{loc}.MPQ with the final table versions delivered by the
// wow-update-{loc}-15211/15354/15595 incremental patches, so these sweeps
// exercise the patch-attachment path end to end. The era is mixed-format:
// most tables still ship as WDBC .dbc, the new ones as WDB2 .db2 — the first
// real-corpus validation of the (previously synthetic-only) WDB2 codec.

TEST_CASE("4.3.4: the full DBC/DB2 corpus decodes and round-trips byte-perfectly",
          "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::cata_client()),
                                  .version = versions::cata,
                                  .locale = tests::cata_locale()});
  REQUIRE(opened.has_value());

  tests::CorpusStats stats;
#define X(Name) \
  tests::sweep_table_mixed<db::tables::Name<versions::cata>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_CATA(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 200);  // 4.3.4 ships ~300 client databases
}

TEST_CASE("4.3.4: Map.dbc spot checks against known truth", "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::cata_client()),
                                  .version = versions::cata,
                                  .locale = tests::cata_locale()});
  REQUIRE(opened.has_value());

  const auto data = opened->read_file(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::cata> map;
  REQUIRE(map.read(*data).has_value());

  const auto by_id = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = by_id(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");

  const auto northrend = by_id(571);
  REQUIRE(northrend != map.records.end());
  CHECK(northrend->directory == "Northrend");

  // Deepholm is Cataclysm content — present here, absent from any 3.3.5a Map.
  const auto deepholm = by_id(646);
  REQUIRE(deepholm != map.records.end());
  CHECK(deepholm->directory == "Deephome");
}

TEST_CASE("4.3.4: Item.db2 (WDB2) decodes through the update chain",
          "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::cata_client()),
                                  .version = versions::cata,
                                  .locale = tests::cata_locale()});
  REQUIRE(opened.has_value());

  const auto data = opened->read_file(FileKey{"DBFilesClient/Item.db2"});
  REQUIRE(data.has_value());

  db::tables::Item<versions::cata> items;
  REQUIRE(items.read(*data).has_value());
  CHECK(items.records.size() > 40'000);  // 4.3.4 knows ~70k items

  // id 25 is the Worn Shortsword every human warrior starts with: a weapon
  // (class 2), one-hand sword (subclass 7) — stable since vanilla.
  const auto sword = std::ranges::find_if(items.records,
                                          [](const auto& r) { return r.id == 25; });
  REQUIRE(sword != items.records.end());
  CHECK(sword->class_id == 2);
  CHECK(sword->subclass_id == 7);
}
