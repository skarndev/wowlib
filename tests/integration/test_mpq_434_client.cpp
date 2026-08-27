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
  const auto locale = tests::cataLocale();
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::cataClient()),
                                  .version = versions::Cata,
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
    const auto lua = storage.readFile(FileKey{"Interface/GlueXML/GlueStrings.lua"});
    REQUIRE(lua.has_value());
    CHECK_FALSE(lua->empty());
  }

  SECTION("a DBC reads with its magic intact (locale base + update deltas)")
  {
    const auto dbc = storage.readFile(FileKey{"DBFilesClient/Map.dbc"});
    REQUIRE(dbc.has_value());
    REQUIRE(dbc->size() >= 4);
    CHECK(std::memcmp(dbc->data(), "WDBC", 4) == 0);
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
