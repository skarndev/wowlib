#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "integration_env.hpp"

using namespace wowlib;

TEST_CASE("the 3.3.5a client opens and serves known files", "[integration][mpq]")
{
  const auto clients = tests::require_clients_dir();

  MpqStorage storage{{.data_dir = clients / tests::mpq_client_name / "Data",
                      .version = ClientVersion::wotlk(),
                      .locale = std::nullopt}};   // exercise auto-detection (enUS)
  REQUIRE(storage.open().has_value());
  CHECK(storage.locale() == Locale::enUS);
  CHECK(storage.archives().size() >= 16);   // full retail chain

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