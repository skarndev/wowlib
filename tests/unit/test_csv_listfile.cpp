#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <wowlib/fs/csv_listfile.hpp>

#include "unit_env.hpp"

using namespace wowlib;
using wowlib::fs::CsvListfile;

namespace
{
  const std::filesystem::path Sample = tests::dataRoot() /
                                       "Sample-listfile.csv";

  // The loaded CSV is the working database registrations write to, so tests
  // always operate on a disposable copy of the committed Sample.
  std::filesystem::path workingCopy(std::string_view name)
  {
    const auto path = std::filesystem::temp_directory_path() / "wowlib-tests" / name;
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::copy_file(Sample, path,
                               std::filesystem::copy_options::overwrite_existing);
    return path;
  }
}

TEST_CASE("loading a listfile resolves both directions", "[listfile]")
{
  auto listfile = CsvListfile::load(Sample);
  REQUIRE(listfile.has_value());
  CHECK(listfile->size() == 6);
  CHECK(listfile->source() == Sample);

  // lookups canonicalize, so any input spelling works
  CHECK(listfile->pathToFdid("DBFilesClient\\Map.db2") == FileDataID{1349477});
  CHECK(listfile->pathToFdid("world/maps/azeroth/azeroth.wdt") == FileDataID{775971});

  // mixed-case CSV content was canonicalized on ingest
  CHECK(listfile->pathToFdid("world/maps/azeroth/azeroth_28_50.adt") ==
        FileDataID{777237});
  CHECK(listfile->fdidToPath(FileDataID{1375801}) ==
        "dbfilesclient\\manifestinterfacedata.db2");

  CHECK(listfile->contains("creature/murloc/murloc.m2"));
  CHECK_FALSE(listfile->contains("no/such/file.m2"));
  CHECK_FALSE(listfile->pathToFdid("no/such/file.m2").has_value());
  CHECK_FALSE(listfile->fdidToPath(FileDataID{1}).has_value());
}

TEST_CASE("malformed lines are reported with their location", "[listfile]")
{
  const auto bad = std::filesystem::temp_directory_path() / "wowlib-tests" /
                   "bad-listfile.csv";
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

TEST_CASE("registration allocates from the configured start and persists to the "
          "working file",
          "[listfile]")
{
  const auto csv = workingCopy("register.csv");

  auto listfile = CsvListfile::load(csv, {.customFdidStart = FileDataID{2'000'000}});
  REQUIRE(listfile.has_value());

  const auto id = listfile->registerPath("world/maps/mymap/MyMap.wdt");
  REQUIRE(id.has_value());
  CHECK(*id == FileDataID{2'000'000});
  CHECK(listfile->pathToFdid("world/maps/mymap/mymap.wdt") == *id);

  // duplicates are refused — community and custom alike
  CHECK(listfile->registerPath("world/maps/mymap/mymap.wdt").error().code ==
        ErrorCode::DuplicatePath);
  CHECK(listfile->registerPath("dbfilesclient/map.db2").error().code ==
        ErrorCode::DuplicatePath);

  // the registration was appended to the working file: a fresh load sees it and
  // the allocator resumes past it
  auto reloaded = CsvListfile::load(csv, {.customFdidStart = FileDataID{2'000'000}});
  REQUIRE(reloaded.has_value());
  CHECK(reloaded->pathToFdid("world/maps/mymap/mymap.wdt") == FileDataID{2'000'000});
  CHECK(reloaded->registerPath("another/new/file.blp").value() == FileDataID{2'000'001});
}

TEST_CASE("save rewrites the working file canonically", "[listfile]")
{
  const auto csv = workingCopy("save.csv");

  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());
  REQUIRE(listfile->registerPath("zz/last.blp").has_value());
  REQUIRE(listfile->save().has_value());

  auto reloaded = CsvListfile::load(csv);
  REQUIRE(reloaded.has_value());
  CHECK(reloaded->size() == listfile->size());
  CHECK(reloaded->contains("zz/last.blp"));
}

TEST_CASE("an in-memory database registers without persistence", "[listfile]")
{
  CsvListfile listfile;   // default: no working file
  const auto id = listfile.registerPath("some/file.blp");
  REQUIRE(id.has_value());
  CHECK(listfile.pathToFdid("some/file.blp") == *id);
}