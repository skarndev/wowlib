#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>

#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("the 4.3.4 client opens and serves known files through the update chain",
          "[integration][mpq]")
{
  const auto locale = tests::cata_locale();
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::cata_client()),
                                  .version = versions::cata,
                                  .locale = locale});
  REQUIRE(opened.has_value());
  MpqStorage& storage = *opened;
  CHECK(storage.locale() == locale);
  // themed base set (base/art/sound/world/expansions) + the locale archives.
  CHECK(storage.archives().size() >= 8);

  SECTION("the fixed base tier opens in table order (base archives first)")
  {
    CHECK(storage.archives().front().path.filename().string().starts_with("base"));
  }

  SECTION("the wow-update tier attached (Cata is the first UpdateChain client)")
  {
    // Updates open as patches on the base archives, not as chain members of
    // their own — visible as the patched flag on the archives they attach to.
    CHECK(std::ranges::any_of(storage.archives(),
                              [](const auto& archive) { return archive.patched; }));
  }

  SECTION("a locale archive file reads")
  {
    const auto lua = storage.read_file(FileKey{"Interface/GlueXML/GlueStrings.lua"});
    REQUIRE(lua.has_value());
    CHECK_FALSE(lua->empty());
  }

  SECTION("a DBC reads with its magic intact (locale base + update deltas)")
  {
    const auto dbc = storage.read_file(FileKey{"DBFilesClient/Map.dbc"});
    REQUIRE(dbc.has_value());
    REQUIRE(dbc->size() >= 4);
    CHECK(std::memcmp(dbc->data(), "WDBC", 4) == 0);
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
