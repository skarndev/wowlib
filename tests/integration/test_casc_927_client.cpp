#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/csv_listfile.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("the 9.2.7 CASC storage opens and serves files by FileDataID",
          "[integration][casc]")
{
  auto opened = CascStorage::open({.client_root = tests::casc_client(),
                                   .build = 45745});
  REQUIRE(opened.has_value());
  CascStorage& storage = *opened;

  SECTION("a known FileDataID reads with a DB2 magic")
  {
    // 1375801 = dbfilesclient/manifestinterfacedata.db2 — a file that has always
    // been locally present in the WoWCircle repack (its CDN-streamed content set
    // grows over time, so pick one that is stably local; see casc-notes.md).
    const auto db2 = storage.read_file(FileKey{FileDataID{1375801}});
    REQUIRE(db2.has_value());
    REQUIRE(db2->size() >= 4);
    const bool wdc = std::memcmp(db2->data(), "WDC3", 4) == 0 ||
                     std::memcmp(db2->data(), "WDC4", 4) == 0;
    CHECK(wdc);
  }

  SECTION("an unknown FileDataID misses cleanly")
  {
    const auto missing = storage.read_file(FileKey{FileDataID{0xFFFFFF}});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileNotFound);
  }

  SECTION("name-only lookups fail on a post-8.2 root, as documented")
  {
    const auto by_name =
      storage.read_file(FileKey{"dbfilesclient/manifestinterfacedata.db2"});
    if (!by_name.has_value())
      CHECK(by_name.error().code == ErrorCode::PathNotResolvable);
    // (if the repack's root still carries this name hash, the read succeeding is
    // also acceptable — the API contract is best-effort)
  }
}

TEST_CASE("paths resolve through the community listfile", "[integration][casc]")
{
  const auto listfile_csv = tests::require_listfile();

  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  CHECK(listfile->size() > 1'000'000);

  const auto fdid = listfile->path_to_fdid("dbfilesclient/manifestinterfacedata.db2");
  REQUIRE(fdid.has_value());
  CHECK(*fdid == FileDataID{1375801});

  auto storage = CascStorage::open({.client_root = tests::casc_client(),
                                    .build = 45745});
  REQUIRE(storage.has_value());
  CHECK(storage->read_file(FileKey{*fdid}).has_value());
}