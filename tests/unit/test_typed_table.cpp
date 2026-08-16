#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/tables/map.hpp>
#include <wowlib/db/typed.hpp>

using namespace wowlib;

namespace
{
  /** A user-declared PROJECTION of Map (wotlk era): two columns by name,
      consteval-validated against the embedded WoWDBDefs blob. */
  struct MapRow
  {
    static constexpr ClientVersion version = versions::wotlk;
    static constexpr std::string_view table_name = "Map";

    std::int32_t id = 0;
    std::string directory;
  };
}

TEST_CASE("typed: a projection loads through the generic table",
          "[db][typed]")
{
  auto typed = db::TypedTable<MapRow>::open();
  REQUIRE(typed.has_value());

  // Author rows through the GENERIC accessors, read them back typed.
  db::DynTable& t = typed->table();
  const auto id = t.column_index("id").value();
  const auto dir = t.column_index("directory").value();
  for (int i = 0; i < 3; ++i)
  {
    const auto row = t.append_row();
    REQUIRE(t.set_int(row, id, 100 + i).has_value());
    REQUIRE(t.set_string(row, dir, "Zone" + std::to_string(i)).has_value());
  }

  const auto rows = typed->load();
  REQUIRE(rows.size() == 3);
  CHECK(rows[1].id == 101);
  CHECK(rows[2].directory == "Zone2");

  SECTION("zero-copy typed column spans")
  {
    const auto ids = typed->column<"id">();
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == 100);
    CHECK(ids[2] == 102);
  }
}

TEST_CASE("typed: a GENERATED record is a valid full-coverage user struct",
          "[db][typed]")
{
  // The 'Both' decision: dbdgen's typed records validate through the exact
  // same consteval path a hand-written struct does — and, covering every
  // column, they may write_back.
  using Record = db::tables::MapRecord<versions::wotlk>;
  auto typed = db::TypedTable<Record>::open();
  REQUIRE(typed.has_value());

  std::vector<Record> rows(2);
  rows[0].id = 7;
  rows[0].directory = "Azeroth";
  REQUIRE(rows[0].map_name.set(Locale::enUS, "Eastern Kingdoms")
              .has_value());
  rows[1].id = 9;
  rows[1].directory = "Kalimdor";
  typed->write_back(rows);
  REQUIRE(typed->table().row_count() == 2);

  const auto back = typed->load();
  REQUIRE(back.size() == 2);
  CHECK(back[0].id == 7);
  CHECK(back[0].directory == "Azeroth");
  CHECK(back[0].map_name.at(Locale::enUS) == "Eastern Kingdoms");
  CHECK(back[1].id == 9);

  // And the round trip holds end-to-end: write bytes, reopen, reload.
  const auto bytes = typed->write();
  REQUIRE(bytes.has_value());
  auto again = db::TypedTable<Record>::open();
  REQUIRE(again.has_value());
  REQUIRE(again->read(*bytes).has_value());
  CHECK(again->load()[1].directory == "Kalimdor");
}
