#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <wowlib/db/tables/manifest_mop.hpp>
#include <wowlib/db/tables/map.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 5.4.8 is the last UpdateChain-era MPQ client and, like 4.3.4, mixed-format:
// most tables ship as WDBC .dbc, the newer ones as WDB2 .db2 — both
// byte-perfect round-trips.

TEST_CASE("5.4.8: the full DBC/DB2 corpus decodes and round-trips byte-perfectly",
          "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::mop_client()),
                                  .version = versions::mop,
                                  .locale = tests::mop_locale()});
  REQUIRE(opened.has_value());

  tests::CorpusStats stats;
#define X(Name) \
  tests::sweep_table_mixed<db::tables::Name<versions::mop>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_MOP(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 250);  // 5.4.8 ships ~330 client databases
}

TEST_CASE("5.4.8: Map.dbc spot checks against known truth", "[integration][db]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::mop_client()),
                                  .version = versions::mop,
                                  .locale = tests::mop_locale()});
  REQUIRE(opened.has_value());

  const auto data = opened->read_file(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::mop> map;
  REQUIRE(map.read(*data).has_value());

  const auto by_id = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = by_id(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");

  // The Pandaria continent is MoP content — absent from any Cata client.
  const auto pandaria = by_id(870);
  REQUIRE(pandaria != map.records.end());
  CHECK(pandaria->directory == "HawaiiMainLand");
}
