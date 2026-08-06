#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("the 1.12.2 client opens and serves known files", "[integration][mpq]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::vanilla_client()),
                                  .version = versions::vanilla,
                                  .locale = tests::vanilla_locale()});
  REQUIRE(opened.has_value());
  MpqStorage& storage = *opened;
  CHECK(storage.locale() == tests::vanilla_locale());
  // base media/data archives (base, dbc, model, terrain, texture, wmo, ...) plus
  // the Data/ patch tier; a vanilla install always yields several archives.
  CHECK(storage.archives().size() >= 8);

  SECTION("a DBC reads with its magic intact")
  {
    const auto dbc = storage.read_file(FileKey{"DBFilesClient/Map.dbc"});
    REQUIRE(dbc.has_value());
    REQUIRE(dbc->size() >= 4);
    CHECK(std::memcmp(dbc->data(), "WDBC", 4) == 0);
  }

  SECTION("the chain opened patches after bases (precedence order on disk)")
  {
    // Override semantics are unit-tested against fake trees; here we assert the
    // real client produced the expected shape: fixed base archives first, the
    // wildcard patch tier last.
    const auto archives = storage.archives();
    CHECK(archives.front().path.filename() == "base.MPQ");
    CHECK(archives.back().path.filename().string().starts_with("patch"));
  }

  SECTION("misses are FileNotFound")
  {
    const auto missing = storage.read_file(FileKey{"no/such/file.blp"});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileNotFound);
  }

  SECTION("id-only requests are rejected on a path-addressed storage")
  {
    const auto by_id = storage.read_file(FileKey{FileDataID{1349477}});
    REQUIRE_FALSE(by_id.has_value());
    CHECK(by_id.error().code == ErrorCode::FdidNotResolvable);
  }
}
