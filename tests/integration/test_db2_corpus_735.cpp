#include <catch2/catch_test_macros.hpp>

#include <wowlib/db/tables/chr_races.hpp>
#include <wowlib/db/tables/creature_display_info_extra.hpp>
#include <wowlib/db/tables/creature_model_data.hpp>
#include <wowlib/db/tables/manifest_interface_data.hpp>
#include <wowlib/db/tables/map.hpp>
#include <wowlib/db/tables/sound_kit.hpp>
#ifdef WOWLIB_DB_FULL_CORPUS
#  include <wowlib/db/tables/manifest_legion.hpp>
#endif

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 7.3.5 is the WDC1 codec's first contact with real client data — every
// prior WDC1 proof was synthetic.
//
// Two halves, because instantiating an era's ~612 generated tables in one TU
// peaks at multiple GB and OOM-kills hosted runners: BREADTH is a structural
// sweep of every shipped .db2 through the raw WDC image parser (no schemas),
// DEPTH is the typed round-trip of representative tables (compression kinds:
// pallet, common, bitpacked, inline strings, multi-section). Configure with
// -DWOWLIB_DB_FULL_CORPUS=ON to additionally sweep every generated table
// typed — the exhaustive check, for machines with the memory to compile it.

TEST_CASE("7.3.5: every shipped DB2 parses structurally (WDC1)",
          "[integration][db]")
{
  const auto csv = tests::requireListfile();
  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::legionClient(),
                                    .build = versions::Legion.build});
  REQUIRE(storage.has_value());

  tests::ImageStats stats;
  tests::sweepDb2Images(*storage, *listfile, csv, stats);

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.parsed == stats.present);
  CHECK(stats.present >= 300);
}

TEST_CASE("7.3.5: representative tables decode and round-trip (WDC1)",
          "[integration][db]")
{
  const auto csv = tests::requireListfile();
  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::legionClient(),
                                    .build = versions::Legion.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
  const auto sweep = [&]<typename Tbl>(std::string_view name) {
    tests::sweepTableCasc<Tbl>(*storage, *listfile, name, stats,
                                 /*bytePerfect=*/false);
  };
  sweep.template operator()<db::tables::Map<versions::Legion>>("Map");
  sweep.template operator()<db::tables::ChrRaces<versions::Legion>>("ChrRaces");
  sweep.template operator()<db::tables::CreatureModelData<versions::Legion>>(
    "CreatureModelData");
  sweep.template operator()<db::tables::CreatureDisplayInfoExtra<versions::Legion>>(
    "CreatureDisplayInfoExtra");
  sweep.template operator()<db::tables::SoundKit<versions::Legion>>("SoundKit");
  sweep.template operator()<db::tables::ManifestInterfaceData<versions::Legion>>(
    "ManifestInterfaceData");

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 4);
}

#ifdef WOWLIB_DB_FULL_CORPUS
TEST_CASE("7.3.5: the FULL DB2 corpus decodes and round-trips (WDC1)",
          "[integration][db][full-corpus]")
{
  const auto csv = tests::requireListfile();
  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::legionClient(),
                                    .build = versions::Legion.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
#  define x(Name)                                                        \
  tests::sweepTableCasc<db::tables::Name<versions::Legion>>(        \
    *storage, *listfile, #Name, stats, /*bytePerfect=*/false);
  WOWLIB_DB_TABLES_LEGION(x)
#  undef x

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 300);
}
#endif
