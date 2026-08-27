#include <catch2/catch_test_macros.hpp>

#include <wowlib/db/tables/manifest_wod.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 6.2.3 is the WDB2 era's CASC face: the same byte-perfect sweep as the
// 4.3.4/5.4.8 MPQ clients, resolved through the community listfile.
TEST_CASE("6.2.3: the full DB2 corpus decodes and round-trips byte-perfectly (WDB2)",
          "[integration][db]")
{
  auto listfile = CsvListfile::load(tests::requireListfile());
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::wodClient(),
                                    .build = versions::Wod.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
#define x(Name)                                                          \
  tests::sweepTableCasc<db::tables::Name<versions::Wod>>(        \
    *storage, *listfile, #Name, stats, /*bytePerfect=*/true);
  WOWLIB_DB_TABLES_WOD(x)
#undef x

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  // 6.2.3 carries 164 of the 165 generated WoD tables (61 as .db2, 103 as
  // .dbc; only DeclinedWordCases is absent) — measured against the install.
  CHECK(stats.present >= 160);
}
