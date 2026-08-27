#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <wowlib/db/schema_catalog.hpp>

using namespace wowlib;

#ifndef WOWLIB_TEST_DBDEFS_DIR
  #error "tests/CMakeLists.txt defines WOWLIB_TEST_DBDEFS_DIR"
#endif

namespace
{
  /** The definitions directory: the build-tree path baked at compile time,
      overridable at runtime — the test binary travels to the integration box,
      where the build tree's absolute path does not exist. */
  std::filesystem::path dbdefsDir()
  {
    if (const char* env = std::getenv("WOWLIB_TEST_DBDEFS_DIR"))
      return env;
    return WOWLIB_TEST_DBDEFS_DIR;
  }
}

TEST_CASE("the runtime WoWDBDefs loader reproduces the embedded catalog",
          "[db][schema][dbd]")
{
  // The parity contract: built from the SAME definitions the build baked,
  // fromDbdDir and the embedded catalog agree schema for schema — every
  // table, every era, every column fact. This is the fence around the
  // C++ port of dbdgen's mangling rules.
  const auto definitions = dbdefsDir();
  if (!std::filesystem::exists(definitions))
    SKIP("no WoWDBDefs checkout at " << definitions.string()
         << " (travelling binary; set WOWLIB_TEST_DBDEFS_DIR)");
  const auto loaded = db::SchemaCatalog::fromDbdDir(definitions);
  REQUIRE(loaded.has_value());
  const db::SchemaCatalog& embedded = db::SchemaCatalog::embedded();

  REQUIRE(loaded->tableCount() == embedded.tableCount());
  REQUIRE(loaded->eras().size() == embedded.eras().size());

  std::size_t compared = 0;
  for (std::size_t i = 0; i < embedded.tableCount(); ++i)
  {
    const std::string_view name = embedded.tableName(i);
    REQUIRE(loaded->tableName(i) == name);
    for (const ClientVersion era : embedded.eras())
    {
      const auto expected = embedded.lookup(name, era);
      const auto actual = loaded->lookup(name, era);
      REQUIRE(expected.has_value() == actual.has_value());
      if (!expected)
        continue;
      INFO("table " << name << " era " << era.major);
      REQUIRE(actual->diskName == expected->diskName);
      REQUIRE(actual->columns.size() == expected->columns.size());
      for (std::size_t c = 0; c < expected->columns.size(); ++c)
      {
        const db::Column& e = expected->columns[c];
        const db::Column& a = actual->columns[c];
        INFO("column " << c << " '" << e.nameView() << "'");
        CHECK(a.nameView() == e.nameView());
        CHECK(a.type == e.type);
        CHECK(a.bits == e.bits);
        CHECK(a.isSigned == e.isSigned);
        CHECK(a.arrayLen == e.arrayLen);
        CHECK(a.localeCount == e.localeCount);
        CHECK(a.isId == e.isId);
        CHECK(a.isRelation == e.isRelation);
        CHECK(a.noninline == e.noninline);
      }
      ++compared;
    }
  }
  CHECK(compared > 4000); // ~1221 tables x their covered eras
}
