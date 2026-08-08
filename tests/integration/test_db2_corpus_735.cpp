#include <catch2/catch_test_macros.hpp>

#include <wowlib/db/tables/manifest_legion.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 7.3.5 is the WDC1 codec's first contact with real data — every prior
// WDC1 proof was synthetic. Semantic round-trip: canonical re-encode,
// re-decode must yield the same record set by id.
TEST_CASE("7.3.5: the full DB2 corpus decodes and round-trips semantically (WDC1)",
          "[integration][db]")
{
  auto listfile = CsvListfile::load(tests::require_listfile());
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = tests::legion_client(),
                                    .build = versions::legion.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
#define X(Name)                                                          \
  tests::sweep_table_casc<db::tables::Name<versions::legion>>(        \
    *storage, *listfile, #Name, stats, /*byte_perfect=*/false);
  WOWLIB_DB_TABLES_LEGION(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 300);
}
