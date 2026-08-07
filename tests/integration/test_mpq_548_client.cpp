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
  const auto locale = tests::mop_locale();
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::mop_client()),
                                  .version = versions::mop,
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
          == tests::data_dir(tests::mop_client()));
  }

  SECTION("the wow-update tier attached (the Cata UpdateChain scheme continues)")
  {
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
