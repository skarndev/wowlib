#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("the 2.4.3 client opens and serves known files", "[integration][mpq]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::tbc_client()),
                                  .version = versions::tbc,
                                  .locale = tests::tbc_locale()});
  REQUIRE(opened.has_value());
  MpqStorage& storage = *opened;
  CHECK(storage.locale() == tests::tbc_locale());
  // common + expansion + the four enGB locale archives + the patch tier.
  CHECK(storage.archives().size() >= 6);

  SECTION("a locale archive file reads")
  {
    const auto lua =
      storage.read_file(FileKey{"Interface/GlueXML/GlueStrings.lua"});
    REQUIRE(lua.has_value());
    CHECK_FALSE(lua->empty());
  }

  SECTION("a DBC reads with its magic intact")
  {
    const auto dbc = storage.read_file(FileKey{"DBFilesClient/Map.dbc"});
    REQUIRE(dbc.has_value());
    REQUIRE(dbc->size() >= 4);
    CHECK(std::memcmp(dbc->data(), "WDBC", 4) == 0);
  }

  SECTION("the chain opened bases before patches (precedence order on disk)")
  {
    // Override semantics are unit-tested against fake trees; here we assert the
    // real client produced the expected shape: the fixed base archives first
    // (common lowest), the wildcard patch tier last.
    const auto archives = storage.archives();
    CHECK(archives.front().path.filename() == "common.MPQ");
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
