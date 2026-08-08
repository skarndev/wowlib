#include <catch2/catch_test_macros.hpp>

#include <wowlib/db/tables/manifest_dragonflight.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 10.2.7 is the WDC5 codec's first contact with real data (prior proofs
// synthetic). Encrypted images preserve verbatim; the rest re-encode
// canonically and must re-decode to the same record set by id.
TEST_CASE("10.2.7: the full DB2 corpus decodes and round-trips semantically (WDC5)",
          "[integration][db]")
{
  auto listfile = CsvListfile::load(tests::require_listfile());
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = tests::df_client(),
                                    .build = versions::dragonflight.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
#define X(Name)                                                          \
  tests::sweep_table_casc<db::tables::Name<versions::dragonflight>>(        \
    *storage, *listfile, #Name, stats, /*byte_perfect=*/false);
  WOWLIB_DB_TABLES_DRAGONFLIGHT(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 500);
}
