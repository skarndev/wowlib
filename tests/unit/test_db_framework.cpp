#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/annotations.hpp>
#include <wowlib/db/locstring.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/db/table.hpp>

using namespace wowlib;

namespace
{
  // A hand-written record exercising every column shape the WDBC engine
  // supports: id, string, localized string, float, int array, narrow ints.
  struct TestRecord
  {
    static constexpr ClientVersion version = versions::wotlk;
    static constexpr std::string_view table_name = "UnitTest";

    [[=db::id]]
    std::uint32_t id = 0;

    std::string name;

    db::LocString8 title;

    float scale = 0.0f;

    std::array<std::uint32_t, 2> flags{};

    std::uint8_t small = 0;

    std::int16_t signed16 = 0;

    bool operator==(const TestRecord&) const = default;
  };

  // 4 (id) + 4 (name) + 36 (8 slots + flags) + 4 (scale) + 8 (flags[2]) + 1 + 2
  constexpr std::size_t test_stride = 59;
  static_assert(db::record_stride<TestRecord>() == test_stride);
  static_assert(db::field_slot_count<TestRecord>() == 1 + 1 + 9 + 1 + 2 + 1 + 1);
  static_assert(db::string_slot_count<TestRecord>() == 1 + 8);

  constexpr auto test_schema = db::schema_of<TestRecord>();
  static_assert(test_schema.size() == 7);
  static_assert(test_schema[0].is_id && test_schema[0].type == db::ColumnType::Int
                && test_schema[0].bits == 32 && !test_schema[0].is_signed);
  static_assert(test_schema[0].name_view() == "id");
  static_assert(test_schema[1].type == db::ColumnType::String);
  static_assert(test_schema[2].type == db::ColumnType::LocString
                && test_schema[2].locale_count == 8);
  static_assert(test_schema[3].type == db::ColumnType::Float);
  static_assert(test_schema[4].type == db::ColumnType::Int && test_schema[4].array_len == 2);
  static_assert(test_schema[5].bits == 8 && !test_schema[5].is_signed);
  static_assert(test_schema[6].bits == 16 && test_schema[6].is_signed);

  /// Append a trivially-copyable value to a byte image under construction.
  template <typename T>
  void append(std::vector<std::byte>& out, const T& value)
  {
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
  }

  /// A synthetic two-record WDBC image with the block "\0Azeroth\0Kalimdor\0".
  std::vector<std::byte> make_image()
  {
    const std::string block{"\0Azeroth\0Kalimdor\0", 18};
    std::vector<std::byte> image;
    append(image, db::wire::wdbc_magic);
    append(image, std::uint32_t{2});                     // record_count
    append(image, db::field_slot_count<TestRecord>());   // field_count
    append(image, std::uint32_t{test_stride});           // record_size
    append(image, static_cast<std::uint32_t>(block.size()));

    // record 1: "Azeroth", enUS title "Kalimdor", assorted values
    append(image, std::uint32_t{1});   // id
    append(image, std::uint32_t{1});   // name -> "Azeroth"
    append(image, std::uint32_t{9});   // title[enUS] -> "Kalimdor"
    for (int slot = 1; slot < 8; ++slot)
      append(image, std::uint32_t{0});
    append(image, std::uint32_t{0xFF01});  // title flags
    append(image, 1.5f);
    append(image, std::uint32_t{3});
    append(image, std::uint32_t{4});
    append(image, std::uint8_t{7});
    append(image, std::int16_t{-5});

    // record 2: shares "Kalimdor", plus a mid-entry suffix reference for enUS
    append(image, std::uint32_t{2});
    append(image, std::uint32_t{9});   // name -> "Kalimdor"
    append(image, std::uint32_t{3});   // title[enUS] -> mid-entry suffix "eroth"
    for (int slot = 1; slot < 8; ++slot)
      append(image, std::uint32_t{0});
    append(image, std::uint32_t{0});
    append(image, 0.25f);
    append(image, std::uint32_t{0});
    append(image, std::uint32_t{0});
    append(image, std::uint8_t{0});
    append(image, std::int16_t{600});

    for (char c : block)
      image.push_back(static_cast<std::byte>(c));
    return image;
  }
}

TEST_CASE("db: WDBC decode reads every column shape", "[db]")
{
  const auto image = make_image();
  db::Table<TestRecord> table;
  REQUIRE(table.read(image).has_value());

  REQUIRE(table.records.size() == 2);
  const TestRecord& first = table.records[0];
  CHECK(first.id == 1);
  CHECK(first.name == "Azeroth");
  CHECK(first.title.at(Locale::enUS) == "Kalimdor");
  CHECK(first.title.at(Locale::enGB) == "Kalimdor");  // enGB shares slot 0
  CHECK(first.title.at(Locale::koKR).empty());
  CHECK(first.title.flags == 0xFF01);
  CHECK(first.scale == 1.5f);
  CHECK(first.flags == std::array<std::uint32_t, 2>{3, 4});
  CHECK(first.small == 7);
  CHECK(first.signed16 == -5);

  const TestRecord& second = table.records[1];
  CHECK(second.name == "Kalimdor");
  CHECK(second.title.at(Locale::enUS) == "eroth");  // shared-tail reference
  CHECK(second.signed16 == 600);

  // ruRU has no slot on an 8-language column
  CHECK(second.title.at(Locale::ruRU).empty());
}

TEST_CASE("db: unmodified WDBC writes back byte-identically", "[db]")
{
  const auto image = make_image();
  db::Table<TestRecord> table;
  REQUIRE(table.read(image).has_value());

  const auto written = table.write();
  REQUIRE(written.has_value());
  REQUIRE(written->size() == image.size());
  CHECK(std::memcmp(written->data(), image.data(), image.size()) == 0);
}

TEST_CASE("db: duplicate string-block entries keep their distinct offsets", "[db]")
{
  // Two identical strings at different offsets; a value->offset lookup alone
  // would collapse both references onto the first copy.
  const std::string block{"\0Foo\0Foo\0", 9};
  std::vector<std::byte> image;
  append(image, db::wire::wdbc_magic);
  append(image, std::uint32_t{2});
  append(image, db::field_slot_count<TestRecord>());
  append(image, std::uint32_t{test_stride});
  append(image, static_cast<std::uint32_t>(block.size()));
  for (std::uint32_t name_offset : {1u, 5u})
  {
    append(image, std::uint32_t{1});
    append(image, name_offset);
    for (int slot = 0; slot < 9; ++slot)
      append(image, std::uint32_t{0});
    append(image, 0.0f);
    append(image, std::uint32_t{0});
    append(image, std::uint32_t{0});
    append(image, std::uint8_t{0});
    append(image, std::int16_t{0});
  }
  for (char c : block)
    image.push_back(static_cast<std::byte>(c));

  db::Table<TestRecord> table;
  REQUIRE(table.read(image).has_value());
  CHECK(table.records[0].name == "Foo");
  CHECK(table.records[1].name == "Foo");

  const auto written = table.write();
  REQUIRE(written.has_value());
  REQUIRE(written->size() == image.size());
  CHECK(std::memcmp(written->data(), image.data(), image.size()) == 0);
}

TEST_CASE("db: modified and added strings dedup then append", "[db]")
{
  const auto image = make_image();
  db::Table<TestRecord> table;
  REQUIRE(table.read(image).has_value());

  table.records[0].name = "Outland";                   // new string: appends
  table.records[1].title.values[1] = "Azeroth";        // existing string: dedups
  TestRecord extra;
  extra.id = 3;
  extra.name = "Outland";                              // reuses the appended copy
  table.records.push_back(extra);

  const auto written = table.write();
  REQUIRE(written.has_value());

  db::Table<TestRecord> reread;
  REQUIRE(reread.read(*written).has_value());
  REQUIRE(reread.records.size() == 3);
  CHECK(reread.records == table.records);

  // "Outland" was appended once, past the original block's end.
  std::uint32_t outland_offset_first = 0;
  std::uint32_t outland_offset_extra = 0;
  std::memcpy(&outland_offset_first, written->data() + 20 + 4, 4);
  std::memcpy(&outland_offset_extra, written->data() + 20 + 2 * test_stride + 4, 4);
  CHECK(outland_offset_first == 18);
  CHECK(outland_offset_extra == outland_offset_first);

  // "Azeroth" resolved to the original entry, not a new copy.
  std::uint32_t azeroth_offset = 0;
  std::memcpy(&azeroth_offset, written->data() + 20 + test_stride + 4 + 4 + 4, 4);
  CHECK(azeroth_offset == 1);
}

TEST_CASE("db: fresh tables derive their header and seed the string block", "[db]")
{
  db::Table<TestRecord> table;
  TestRecord record;
  record.id = 42;
  record.name = "New";
  REQUIRE(record.title.set(Locale::deDE, "Neu").has_value());
  table.records.push_back(record);

  const auto written = table.write();
  REQUIRE(written.has_value());

  db::wire::WdbcHeader header;
  std::memcpy(&header, written->data(), sizeof header);
  CHECK(header.record_count == 1);
  CHECK(header.field_count == db::field_slot_count<TestRecord>());
  CHECK(header.record_size == test_stride);
  CHECK((*written)[sizeof header + test_stride] == std::byte{0});  // leading zero byte

  db::Table<TestRecord> reread;
  REQUIRE(reread.read(*written).has_value());
  REQUIRE(reread.records.size() == 1);
  CHECK(reread.records[0] == record);
  CHECK(reread.records[0].title.at(Locale::deDE) == "Neu");
}

TEST_CASE("db: malformed images error with the right codes", "[db]")
{
  const auto image = make_image();

  SECTION("unknown magic")
  {
    auto bad = image;
    bad[3] = std::byte{'X'};
    db::Table<TestRecord> table;
    const auto r = table.read(bad);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::TableMagicUnknown);
  }

  SECTION("record_size disagrees with the schema")
  {
    auto bad = image;
    const std::uint32_t wrong = test_stride + 4;
    std::memcpy(bad.data() + 12, &wrong, 4);
    db::Table<TestRecord> table;
    const auto r = table.read(bad);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::SchemaMismatch);
  }

  SECTION("truncated body")
  {
    auto bad = image;
    bad.pop_back();
    db::Table<TestRecord> table;
    const auto r = table.read(bad);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::TableTruncated);
  }

  SECTION("too small for a header")
  {
    db::Table<TestRecord> table;
    const auto r = table.read(std::span<const std::byte>{image.data(), 7});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::TableTruncated);
  }
}

TEST_CASE("db: locale slots without a column reject writes", "[db]")
{
  db::LocString8 vanilla_column;
  const auto r = vanilla_column.set(Locale::ruRU, "нет");
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == ErrorCode::NotSupported);

  db::LocString16 tbc_column;
  REQUIRE(tbc_column.set(Locale::ruRU, "да").has_value());
  CHECK(tbc_column.at(Locale::ruRU) == "да");
}
