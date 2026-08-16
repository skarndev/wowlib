#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/db/schema_catalog.hpp>
#include <wowlib/db/tables/map.hpp>

using namespace wowlib;

namespace
{
  /** The generated-header schema and the catalog schema must agree column for
      column — the parity that lets the generic engine replace the generated
      types without changing what reaches the codecs. */
  void require_matches(std::span<const db::Column> expected,
                       std::span<const db::Column> actual)
  {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
      INFO("column " << i << " '" << expected[i].name_view() << "'");
      CHECK(actual[i].name_view() == expected[i].name_view());
      CHECK(actual[i].type == expected[i].type);
      CHECK(actual[i].bits == expected[i].bits);
      CHECK(actual[i].is_signed == expected[i].is_signed);
      CHECK(actual[i].array_len == expected[i].array_len);
      CHECK(actual[i].locale_count == expected[i].locale_count);
      CHECK(actual[i].is_id == expected[i].is_id);
      CHECK(actual[i].is_relation == expected[i].is_relation);
      CHECK(actual[i].noninline == expected[i].noninline);
      CHECK(actual[i].inline_bytes() == expected[i].inline_bytes());
      CHECK(actual[i].field_slots() == expected[i].field_slots());
    }
  }
}

TEST_CASE("embedded catalog matches the generated schema (Map)", "[db][schema]")
{
  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();

  SECTION("wotlk")
  {
    const auto schema = catalog.lookup("Map", versions::wotlk);
    REQUIRE(schema.has_value());
    CHECK(schema->name == "Map");
    CHECK(schema->disk_name == "Map");
    static constexpr auto expected =
        db::schema_of<db::tables::MapRecord<versions::wotlk>>();
    require_matches(expected, schema->columns);
  }

  SECTION("vanilla differs (LocString8 era)")
  {
    const auto schema = catalog.lookup("Map", versions::vanilla);
    REQUIRE(schema.has_value());
    static constexpr auto expected =
        db::schema_of<db::tables::MapRecord<versions::vanilla>>();
    require_matches(expected, schema->columns);
  }
}

TEST_CASE("catalog resolution failures are designed", "[db][schema]")
{
  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();

  SECTION("unknown table")
  {
    const auto schema = catalog.lookup("NoSuchTable", versions::wotlk);
    REQUIRE_FALSE(schema.has_value());
    CHECK(schema.error().code == ErrorCode::TableUnknown);
  }

  SECTION("unknown era major")
  {
    const auto schema =
        catalog.lookup("Map", ClientVersion{99, 0, 0, 1});
    REQUIRE_FALSE(schema.has_value());
    CHECK(schema.error().code == ErrorCode::UnsupportedClientVersion);
  }
}

TEST_CASE("catalog enumeration and era table", "[db][schema]")
{
  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();
  REQUIRE(catalog.table_count() > 1000); // all-era WoWDBDefs coverage
  // Name-sorted directory: strictly ascending names.
  for (std::size_t i = 1; i < catalog.table_count(); ++i)
    REQUIRE(catalog.table_name(i - 1) < catalog.table_name(i));
  REQUIRE(catalog.eras().size() == 11);
  CHECK(catalog.eras().front() == versions::vanilla);
  CHECK(catalog.eras().back().major == 11);
}

TEST_CASE("malformed blobs are rejected", "[db][schema]")
{
  SECTION("garbage")
  {
    std::vector<unsigned char> bytes{'n', 'o', 'p', 'e'};
    const auto catalog = db::SchemaCatalog::from_blob(std::move(bytes));
    REQUIRE_FALSE(catalog.has_value());
    CHECK(catalog.error().code == ErrorCode::SchemaBlobInvalid);
  }

  SECTION("empty")
  {
    const auto catalog = db::SchemaCatalog::from_blob({});
    REQUIRE_FALSE(catalog.has_value());
    CHECK(catalog.error().code == ErrorCode::SchemaBlobInvalid);
  }
}
