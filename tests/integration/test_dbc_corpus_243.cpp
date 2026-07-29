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
  const auto clients = tests::require_clients_dir();
  auto opened = MpqStorage::open({.data_dir = clients / tests::tbc_client_name / "Data",
                                  .version = versions::tbc,
                                  .locale = tests::tbc_locale});
  REQUIRE(opened.has_value());

  tests::CorpusStats stats;
#define X(Name) \
  tests::sweep_table<db::tables::Name<versions::tbc>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_TBC(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 100);
}

TEST_CASE("2.4.3: Map.dbc spot checks against known truth", "[integration][db]")
{
  const auto clients = tests::require_clients_dir();
  auto opened = MpqStorage::open({.data_dir = clients / tests::tbc_client_name / "Data",
                                  .version = versions::tbc,
                                  .locale = tests::tbc_locale});
  REQUIRE(opened.has_value());

  const auto data = opened->read_file(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::tbc> map;
  REQUIRE(map.read(*data).has_value());

  const auto by_id = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = by_id(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");
  CHECK_FALSE(azeroth->map_name.at(tests::tbc_locale).empty());

  const auto outland = by_id(530);
  REQUIRE(outland != map.records.end());
  CHECK(outland->directory == "Expansion01");
}
