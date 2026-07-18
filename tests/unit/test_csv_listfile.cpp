#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <wowlib/fs/csv_listfile.hpp>

using namespace wowlib;

namespace
{
  const std::filesystem::path sample = std::filesystem::path{WOWLIB_TEST_DATA_DIR} /
                                       "sample-listfile.csv";

  std::filesystem::path temp_file(std::string_view name)
  {
    return std::filesystem::temp_directory_path() / "wowlib-tests" / name;
  }
}

TEST_CASE("loading the sample listfile resolves both directions", "[listfile]")
{
  auto listfile = CsvListfile::load(sample);
  REQUIRE(listfile.has_value());
  CHECK(listfile->size() == 6);

  // lookups canonicalize, so any input spelling works
  CHECK(listfile->path_to_fdid("DBFilesClient\\Map.db2") == FileDataID{1349477});
  CHECK(listfile->path_to_fdid("world/maps/azeroth/azeroth.wdt") == FileDataID{775971});

  // mixed-case CSV content was canonicalized on ingest
  CHECK(listfile->path_to_fdid("world/maps/azeroth/azeroth_28_50.adt") ==
        FileDataID{777237});
  CHECK(listfile->fdid_to_path(FileDataID{1375801}) ==
        "dbfilesclient\\manifestinterfacedata.db2");

  CHECK(listfile->contains("creature/murloc/murloc.m2"));
  CHECK_FALSE(listfile->contains("no/such/file.m2"));
  CHECK_FALSE(listfile->path_to_fdid("no/such/file.m2").has_value());
  CHECK_FALSE(listfile->fdid_to_path(FileDataID{1}).has_value());
}

TEST_CASE("malformed lines are reported with their location", "[listfile]")
{
  const auto bad = temp_file("bad-listfile.csv");
  std::filesystem::create_directories(bad.parent_path());
  std::ofstream{bad} << "123;ok/path.blp\nnot-a-number;foo.blp\n";

  const auto listfile = CsvListfile::load(bad);
  REQUIRE_FALSE(listfile.has_value());
  CHECK(listfile.error().code == ErrorCode::ListfileParseError);
  CHECK(listfile.error().message.contains("line 2"));
}

TEST_CASE("a missing listfile is an io error", "[listfile]")
{
  const auto listfile = CsvListfile::load("/no/such/listfile.csv");
  REQUIRE_FALSE(listfile.has_value());
  CHECK(listfile.error().code == ErrorCode::ListfileIoError);
}

TEST_CASE("registration allocates from the configured start and persists", "[listfile]")
{
  auto listfile = CsvListfile::load(sample, {.custom_fdid_start = FileDataID{2'000'000}});
  REQUIRE(listfile.has_value());

  const auto id = listfile->register_path("world/maps/mymap/MyMap.wdt");
  REQUIRE(id.has_value());
  CHECK(*id == FileDataID{2'000'000});
  CHECK(listfile->path_to_fdid("world/maps/mymap/mymap.wdt") == *id);

  // duplicates are refused — community and custom alike
  CHECK(listfile->register_path("world/maps/mymap/mymap.wdt").error().code ==
        ErrorCode::DuplicatePath);
  CHECK(listfile->register_path("dbfilesclient/map.db2").error().code ==
        ErrorCode::DuplicatePath);

  // sidecar round-trip: only the custom entry is saved; reloading bumps the allocator
  const auto sidecar = temp_file("sidecar.csv");
  REQUIRE(listfile->save_custom_entries(sidecar).has_value());

  auto fresh = CsvListfile::load(sample, {.custom_fdid_start = FileDataID{2'000'000}});
  REQUIRE(fresh.has_value());
  REQUIRE(fresh->load_custom_entries(sidecar).has_value());
  CHECK(fresh->path_to_fdid("world/maps/mymap/mymap.wdt") == FileDataID{2'000'000});
  CHECK(fresh->register_path("another/new/file.blp").value() == FileDataID{2'000'001});
}