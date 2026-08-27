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

namespace
{
  /** Whether an install's client databases are the 5.1+ layouts dbdgen
      generates for versions::mop (5.4.8.18414).

      Some "5.4.8" distributions ship 5.0.x-era DBFilesClient: their update
      archives carry only audio, so every table keeps its original MoP-launch
      layout (verified on the CI box: AreaTable at 112 bytes instead of 120 —
      no WildBattlePetLevelMin/Max — and Map.dbc topping out at id 1076 with
      neither Throne of Thunder nor Siege of Orgrimmar). Nothing in wowlib can
      decode those against 5.4.8 schemas, and pretending otherwise would
      report a hundred bogus failures, so the sweeps skip instead.
      @param storage the client's MPQ chain.
      @return true when the databases match the targeted 5.4.8 layouts. */
  bool databasesAre54(fs::MpqStorage& storage)
  {
    const auto data = storage.readFile(FileKey{"DBFilesClient/AreaTable.dbc"});
    if (!data || data->size() < 20)
      return false;
    std::uint32_t recordSize = 0;
    std::memcpy(&recordSize, data->data() + 12, sizeof recordSize);
    return recordSize == 120;
  }
}

TEST_CASE("5.4.8: the full DBC/DB2 corpus decodes and round-trips byte-perfectly",
          "[integration][db]")
{
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mopClient()),
                                  .version = versions::Mop,
                                  .locale = tests::mopLocale()});
  REQUIRE(opened.has_value());
  if (!databasesAre54(*opened))
    SKIP("this 5.4.8 install ships 5.0.x-era client databases (AreaTable is "
         "112 bytes, the pre-5.1 layout) — dbdgen targets 5.4.8.18414");

  tests::CorpusStats stats;
#define X(Name) \
  tests::sweepTableMixed<db::tables::Name<versions::Mop>>(*opened, #Name, stats);
  WOWLIB_DB_TABLES_MOP(X)
#undef X

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 250);  // 5.4.8 ships ~330 client databases
}

TEST_CASE("5.4.8: Map.dbc spot checks against known truth", "[integration][db]")
{
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mopClient()),
                                  .version = versions::Mop,
                                  .locale = tests::mopLocale()});
  REQUIRE(opened.has_value());

  if (!databasesAre54(*opened))
    SKIP("this 5.4.8 install ships 5.0.x-era client databases");

  const auto data = opened->readFile(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());
  db::tables::Map<versions::Mop> map;
  REQUIRE(map.read(*data).has_value());

  const auto byId = [&](std::int32_t id) {
    return std::ranges::find_if(map.records, [&](const auto& r) { return r.id == id; });
  };

  const auto azeroth = byId(0);
  REQUIRE(azeroth != map.records.end());
  CHECK(azeroth->directory == "Azeroth");

  // The Pandaria continent is MoP content — absent from any Cata client.
  const auto pandaria = byId(870);
  REQUIRE(pandaria != map.records.end());
  CHECK(pandaria->directory == "HawaiiMainLand");
}
