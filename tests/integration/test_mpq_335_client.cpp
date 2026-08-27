#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("the 3.3.5a client opens and serves known files", "[integration][mpq]")
{
  const auto locale = tests::mpqLocale();
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mpqClient()),
                                  .version = versions::Wotlk,
                                  .locale = locale});
  REQUIRE(opened.has_value());
  MpqStorage& storage = *opened;
  CHECK(storage.locale() == locale);
  CHECK(storage.archives().size() >= 16);   // full retail chain

  SECTION("a locale archive file reads")
  {
    const auto lua =
      storage.readFile(FileKey{"Interface/GlueXML/GlueStrings.lua"});
    REQUIRE(lua.has_value());
    CHECK_FALSE(lua->empty());
  }

  SECTION("a DBC reads with its magic intact")
  {
    const auto dbc = storage.readFile(FileKey{"DBFilesClient/Map.dbc"});
    REQUIRE(dbc.has_value());
    REQUIRE(dbc->size() >= 4);
    CHECK(std::memcmp(dbc->data(), "WDBC", 4) == 0);
  }

  SECTION("the chain opened patches after bases (precedence order on disk)")
  {
    // Override semantics (reverse-order search) are unit-tested against fake
    // trees; here we assert the real client produced the expected shape: bases
    // first, patches last.
    const auto archives = storage.archives();
    CHECK(archives.front().path.filename() == "common.MPQ");
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