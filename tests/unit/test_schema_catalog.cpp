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
  void requireMatches(std::span<const db::Column> expected,
                       std::span<const db::Column> actual)
  {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
      INFO("column " << i << " '" << expected[i].nameView() << "'");
      CHECK(actual[i].nameView() == expected[i].nameView());
      CHECK(actual[i].type == expected[i].type);
      CHECK(actual[i].bits == expected[i].bits);
      CHECK(actual[i].isSigned == expected[i].isSigned);
      CHECK(actual[i].arrayLen == expected[i].arrayLen);
      CHECK(actual[i].localeCount == expected[i].localeCount);
      CHECK(actual[i].isId == expected[i].isId);
      CHECK(actual[i].isRelation == expected[i].isRelation);
      CHECK(actual[i].noninline == expected[i].noninline);
      CHECK(actual[i].inlineBytes() == expected[i].inlineBytes());
      CHECK(actual[i].fieldSlots() == expected[i].fieldSlots());
    }
  }
}

TEST_CASE("embedded catalog matches the generated schema (Map)", "[db][schema]")
{
  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();

  SECTION("wotlk")
  {
    const auto schema = catalog.lookup("Map", versions::Wotlk);
    REQUIRE(schema.has_value());
    CHECK(schema->name == "Map");
    CHECK(schema->diskName == "Map");
    static constexpr auto Expected =
        db::schemaOf<db::tables::MapRecord<versions::Wotlk>>();
    requireMatches(Expected, schema->columns);
  }

  SECTION("vanilla differs (LocString8 era)")
  {
    const auto schema = catalog.lookup("Map", versions::Vanilla);
    REQUIRE(schema.has_value());
    static constexpr auto Expected =
        db::schemaOf<db::tables::MapRecord<versions::Vanilla>>();
    requireMatches(Expected, schema->columns);
  }
}

TEST_CASE("catalog resolution failures are designed", "[db][schema]")
{
  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();

  SECTION("unknown table")
  {
    const auto schema = catalog.lookup("NoSuchTable", versions::Wotlk);
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
  REQUIRE(catalog.tableCount() > 1000); // all-era WoWDBDefs coverage
  // Name-sorted directory: strictly ascending names.
  for (std::size_t i = 1; i < catalog.tableCount(); ++i)
    REQUIRE(catalog.tableName(i - 1) < catalog.tableName(i));
  REQUIRE(catalog.eras().size() == 11);
  CHECK(catalog.eras().front() == versions::Vanilla);
  CHECK(catalog.eras().back().major == 11);
}

TEST_CASE("malformed blobs are rejected", "[db][schema]")
{
  SECTION("garbage")
  {
    std::vector<unsigned char> bytes{'n', 'o', 'p', 'e'};
    const auto catalog = db::SchemaCatalog::fromBlob(std::move(bytes));
    REQUIRE_FALSE(catalog.has_value());
    CHECK(catalog.error().code == ErrorCode::SchemaBlobInvalid);
  }

  SECTION("empty")
  {
    const auto catalog = db::SchemaCatalog::fromBlob({});
    REQUIRE_FALSE(catalog.has_value());
    CHECK(catalog.error().code == ErrorCode::SchemaBlobInvalid);
  }
}
