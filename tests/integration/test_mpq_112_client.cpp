#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("the 1.12.2 client opens and serves known files", "[integration][mpq]")
{
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::vanillaClient()),
                                  .version = versions::Vanilla,
                                  .locale = tests::vanillaLocale()});
  REQUIRE(opened.has_value());
  MpqStorage& storage = *opened;
  CHECK(storage.locale() == tests::vanillaLocale());
  // base media/data archives (base, dbc, model, terrain, texture, wmo, ...) plus
  // the Data/ patch tier; a vanilla install always yields several archives.
  CHECK(storage.archives().size() >= 8);

  SECTION("a DBC reads with its magic intact")
  {
    const auto dbc = storage.readFile(FileKey{"DBFilesClient/Map.dbc"});
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
    const auto missing = storage.readFile(FileKey{"no/such/file.blp"});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileNotFound);
  }

  SECTION("id-only requests are rejected on a path-addressed storage")
  {
    const auto byId = storage.readFile(FileKey{FileDataID{1349477}});
    REQUIRE_FALSE(byId.has_value());
    CHECK(byId.error().code == ErrorCode::FdidNotResolvable);
  }
}
