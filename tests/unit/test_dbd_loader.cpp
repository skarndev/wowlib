#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

#include <wowlib/db/schema_catalog.hpp>

using namespace wowlib;

#ifndef WOWLIB_TEST_DBDEFS_DIR
  #error "tests/CMakeLists.txt defines WOWLIB_TEST_DBDEFS_DIR"
#endif

TEST_CASE("the runtime WoWDBDefs loader reproduces the embedded catalog",
          "[db][schema][dbd]")
{
  // The parity contract: built from the SAME definitions the build baked,
  // from_dbd_dir and the embedded catalog agree schema for schema — every
  // table, every era, every column fact. This is the fence around the
  // C++ port of dbdgen's mangling rules.
  const auto loaded = db::SchemaCatalog::from_dbd_dir(WOWLIB_TEST_DBDEFS_DIR);
  REQUIRE(loaded.has_value());
  const db::SchemaCatalog& embedded = db::SchemaCatalog::embedded();

  REQUIRE(loaded->table_count() == embedded.table_count());
  REQUIRE(loaded->eras().size() == embedded.eras().size());

  std::size_t compared = 0;
  for (std::size_t i = 0; i < embedded.table_count(); ++i)
  {
    const std::string_view name = embedded.table_name(i);
    REQUIRE(loaded->table_name(i) == name);
    for (const ClientVersion era : embedded.eras())
    {
      const auto expected = embedded.lookup(name, era);
      const auto actual = loaded->lookup(name, era);
      REQUIRE(expected.has_value() == actual.has_value());
      if (!expected)
        continue;
      INFO("table " << name << " era " << era.major);
      REQUIRE(actual->disk_name == expected->disk_name);
      REQUIRE(actual->columns.size() == expected->columns.size());
      for (std::size_t c = 0; c < expected->columns.size(); ++c)
      {
        const db::Column& e = expected->columns[c];
        const db::Column& a = actual->columns[c];
        INFO("column " << c << " '" << e.name_view() << "'");
        CHECK(a.name_view() == e.name_view());
        CHECK(a.type == e.type);
        CHECK(a.bits == e.bits);
        CHECK(a.is_signed == e.is_signed);
        CHECK(a.array_len == e.array_len);
        CHECK(a.locale_count == e.locale_count);
        CHECK(a.is_id == e.is_id);
        CHECK(a.is_relation == e.is_relation);
        CHECK(a.noninline == e.noninline);
      }
      ++compared;
    }
  }
  CHECK(compared > 4000); // ~1221 tables x their covered eras
}
