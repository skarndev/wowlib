#include <catch2/catch_test_macros.hpp>

#include <wowlib/db/tables/chr_races.hpp>
#include <wowlib/db/tables/creature_display_info_extra.hpp>
#include <wowlib/db/tables/creature_model_data.hpp>
#include <wowlib/db/tables/manifest_interface_data.hpp>
#include <wowlib/db/tables/map.hpp>
#include <wowlib/db/tables/sound_kit.hpp>
#ifdef WOWLIB_DB_FULL_CORPUS
#  include <wowlib/db/tables/manifest_dragonflight.hpp>
#endif

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 10.2.7 is the WDC5 codec's first contact with real client data (prior
// proofs synthetic).
//
// Two halves, because instantiating an era's ~987 generated tables in one TU
// peaks at multiple GB and OOM-kills hosted runners: BREADTH is a structural
// sweep of every shipped .db2 through the raw WDC image parser (no schemas),
// DEPTH is the typed round-trip of representative tables (compression kinds:
// pallet, common, bitpacked, inline strings, multi-section). Configure with
// -DWOWLIB_DB_FULL_CORPUS=ON to additionally sweep every generated table
// typed — the exhaustive check, for machines with the memory to compile it.

TEST_CASE("10.2.7: every shipped DB2 parses structurally (WDC5)",
          "[integration][db]")
{
  const auto csv = tests::requireListfile();
  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::dfClient(),
                                    .build = versions::Dragonflight.build});
  REQUIRE(storage.has_value());

  tests::ImageStats stats;
  tests::sweepDb2Images(*storage, *listfile, csv, stats);

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.parsed == stats.present);
  CHECK(stats.present >= 500);
}

TEST_CASE("10.2.7: representative tables decode and round-trip (WDC5)",
          "[integration][db]")
{
  const auto csv = tests::requireListfile();
  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::dfClient(),
                                    .build = versions::Dragonflight.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
  const auto sweep = [&]<typename Tbl>(std::string_view name) {
    tests::sweepTableCasc<Tbl>(*storage, *listfile, name, stats,
                                 /*bytePerfect=*/false);
  };
  sweep.template operator()<db::tables::Map<versions::Dragonflight>>("Map");
  sweep.template operator()<db::tables::ChrRaces<versions::Dragonflight>>("ChrRaces");
  sweep.template operator()<db::tables::CreatureModelData<versions::Dragonflight>>(
    "CreatureModelData");
  sweep.template operator()<db::tables::CreatureDisplayInfoExtra<versions::Dragonflight>>(
    "CreatureDisplayInfoExtra");
  sweep.template operator()<db::tables::SoundKit<versions::Dragonflight>>("SoundKit");
  sweep.template operator()<db::tables::ManifestInterfaceData<versions::Dragonflight>>(
    "ManifestInterfaceData");

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 4);
}

#ifdef WOWLIB_DB_FULL_CORPUS
TEST_CASE("10.2.7: the FULL DB2 corpus decodes and round-trips (WDC5)",
          "[integration][db][full-corpus]")
{
  const auto csv = tests::requireListfile();
  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::dfClient(),
                                    .build = versions::Dragonflight.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
#  define x(Name)                                                        \
  tests::sweepTableCasc<db::tables::Name<versions::Dragonflight>>(        \
    *storage, *listfile, #Name, stats, /*bytePerfect=*/false);
  WOWLIB_DB_TABLES_DRAGONFLIGHT(x)
#  undef x

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 500);
}
#endif
