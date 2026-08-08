#include <catch2/catch_test_macros.hpp>

#include <wowlib/db/tables/manifest_bfa.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

// 8.3.7 WDC3 breadth sweep; the 9.2.7 suite (test_db2_corpus_927.cpp)
// covers the WDC3 edge cases in depth.
TEST_CASE("8.3.7: the full DB2 corpus decodes and round-trips semantically (WDC3)",
          "[integration][db]")
{
  auto listfile = CsvListfile::load(tests::require_listfile());
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = tests::bfa_client(),
                                    .build = versions::bfa.build});
  REQUIRE(storage.has_value());

  tests::CorpusStats stats;
#define X(Name)                                                          \
  tests::sweep_table_casc<db::tables::Name<versions::bfa>>(        \
    *storage, *listfile, #Name, stats, /*byte_perfect=*/false);
  WOWLIB_DB_TABLES_BFA(X)
#undef X

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 400);
}
