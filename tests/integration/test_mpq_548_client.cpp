#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>

#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;

TEST_CASE("the 5.4.8 client opens and serves known files through the update chain",
          "[integration][mpq]")
{
  const auto locale = tests::mopLocale();
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mopClient()),
                                  .version = versions::Mop,
                                  .locale = locale});
  REQUIRE(opened.has_value());
  MpqStorage& storage = *opened;
  CHECK(storage.locale() == locale);
  // MoP's themed base set (interface/itemtexture/misc/model/texture join the
  // Cata scheme, expansion4 arrives) + the locale archives.
  CHECK(storage.archives().size() >= 10);

  SECTION("the fixed base tier opens before the locale tier")
  {
    // Missing table rows are skipped, so the front archive is whichever fixed
    // Data-root member the install carries (the CI fleet's 5.4.8 ships no
    // base-*.MPQ at all — its chain starts at interface.MPQ) — never an
    // archive from the Data/{locale}/ tier.
    CHECK(storage.archives().front().path.parent_path()
          == tests::dataDir(tests::mopClient()));
  }

  SECTION("the wow-update tier attached (the Cata UpdateChain scheme continues)")
  {
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
