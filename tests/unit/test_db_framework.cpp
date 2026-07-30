#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/annotations.hpp>
#include <wowlib/db/locstring.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/db/table.hpp>
#include <wowlib/db/wire/wdc3.hpp>

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

TEST_CASE("db: the WDC3 bit reader extracts fields at arbitrary bit offsets", "[db]")
{
  // A byte pattern with known bit runs. Little-endian: byte 0 = 0xB4, etc.
  const std::array<std::byte, 8> bytes{
    std::byte{0xB4}, std::byte{0x9A}, std::byte{0x78}, std::byte{0x56},
    std::byte{0x34}, std::byte{0x12}, std::byte{0xF0}, std::byte{0xFF}};
  db::wire::BitReader reader{bytes.data(), bytes.size()};

  CHECK(reader.read(0, 8) == 0xB4);
  CHECK(reader.read(8, 8) == 0x9A);
  CHECK(reader.read(0, 4) == 0x4);       // low nibble of 0xB4
  CHECK(reader.read(4, 4) == 0xB);       // high nibble of 0xB4
  CHECK(reader.read(0, 16) == 0x9AB4);   // little-endian u16
  CHECK(reader.read(16, 32) == 0x12345678);
  CHECK(reader.read(12, 12) == 0x789);   // spans bytes 1..2, shifted

  // A 32-bit read straddling a non-byte boundary: 0xFFF0123456789AB4 >> 4,
  // low 32 bits.
  CHECK(reader.read(4, 32) == 0x456789AB);

  // Reads that would overrun the buffer clamp to 0.
  CHECK(reader.read(60, 32) == 0);
}

namespace
{
  // Sign extension is applied to the signed compression kind; verify the helper
  // indirectly via a bit read + manual extension mirroring the engine.
  std::int64_t sign_extend(std::uint64_t raw, std::size_t bits)
  {
    const std::uint64_t sign = std::uint64_t{1} << (bits - 1);
    if (raw & sign)
      return static_cast<std::int64_t>(raw | ~((std::uint64_t{1} << bits) - 1));
    return static_cast<std::int64_t>(raw);
  }
}

TEST_CASE("db: WDC3 signed field bit extension", "[db]")
{
  // 6-bit value 0x3F = -1; 0x20 = -32; 0x1F = 31.
  CHECK(sign_extend(0x3F, 6) == -1);
  CHECK(sign_extend(0x20, 6) == -32);
  CHECK(sign_extend(0x1F, 6) == 31);
  CHECK(sign_extend(0x00, 6) == 0);
}

namespace
{
  // A BfA+ (WDC3-era) record: non-inline id, strings, floats, arrays, signed
  // and narrow ints — the mix a canonical write must round-trip.
  struct WdcRecord
  {
    static constexpr ClientVersion version = versions::shadowlands;
    static constexpr std::string_view table_name = "WdcUnitTest";

    [[=db::id, =db::noninline]]
    std::int32_t id = 0;

    std::string name;
    float scale = 0.0f;
    std::array<std::uint16_t, 3> flags{};
    std::int8_t bias = 0;
    std::array<std::string, 2> tags{};

    bool operator==(const WdcRecord&) const = default;
  };
}

TEST_CASE("db: a fresh WDC3 table writes and semantically round-trips", "[db]")
{
  db::Table<WdcRecord> table;
  table.records.push_back(WdcRecord{.id = 100, .name = "Alpha", .scale = 2.5f,
                                    .flags = {1, 2, 3}, .bias = -7, .tags = {"x", "yy"}});
  table.records.push_back(WdcRecord{.id = 5, .name = "", .scale = -0.25f,
                                    .flags = {0, 0, 65535}, .bias = 42, .tags = {"", "z"}});
  table.records.push_back(WdcRecord{.id = 999, .name = "Alpha", .scale = 0.0f,  // dedup "Alpha"
                                    .flags = {9, 9, 9}, .bias = 0, .tags = {"x", "x"}});

  const auto written = table.write();
  REQUIRE(written.has_value());
  CHECK(std::memcmp(written->data(), "WDC3", 4) == 0);

  db::Table<WdcRecord> reread;
  REQUIRE(reread.read(*written).has_value());
  CHECK(reread.fully_decoded());
  REQUIRE(reread.records.size() == 3);
  CHECK(reread.records == table.records);

  // Non-inline id came back through the id_list; a signed negative survived.
  CHECK(reread.records[0].id == 100);
  CHECK(reread.records[0].bias == -7);
  CHECK(reread.records[1].flags[2] == 65535);
}

namespace
{
  // One wide (uint32) column holding only small values — the case bitpacking
  // must shrink rather than store at full 4-byte width.
  struct WdcSmallRecord
  {
    static constexpr ClientVersion version = versions::shadowlands;
    static constexpr std::string_view table_name = "WdcSmall";

    [[=db::id, =db::noninline]]
    std::int32_t id = 0;

    std::uint32_t value = 0;

    bool operator==(const WdcSmallRecord&) const = default;
  };
}

namespace
{
  // A BfA+ record whose id is INLINE ($id$ without $noninline$): the writer must
  // keep it as a record field with flag 0x00, not move it to an id_list.
  struct WdcInlineIdRecord
  {
    static constexpr ClientVersion version = versions::shadowlands;
    static constexpr std::string_view table_name = "WdcInlineId";

    [[=db::id]]
    std::int32_t id = 0;

    std::int32_t value = 0;
    std::string name;

    bool operator==(const WdcInlineIdRecord&) const = default;
  };
}

TEST_CASE("db: WDC3 write keeps an inline id in the record, not an id_list", "[db]")
{
  db::Table<WdcInlineIdRecord> table;
  table.records.push_back(WdcInlineIdRecord{.id = 7, .value = 100, .name = "a"});
  table.records.push_back(WdcInlineIdRecord{.id = 42, .value = -3, .name = "b"});
  table.records.push_back(WdcInlineIdRecord{.id = 99, .value = 100, .name = "c"});

  const auto written = table.write();
  REQUIRE(written.has_value());

  db::wire::Wdc3Header header;
  std::memcpy(&header, written->data(), sizeof header);
  CHECK((header.flags & db::wire::wdc3_flag_noninline_id) == 0);  // id stays inline
  db::wire::Wdc3SectionHeader section;
  std::memcpy(&section, written->data() + sizeof header, sizeof section);
  CHECK(section.id_list_size == 0);  // no id_list for an inline id

  db::Table<WdcInlineIdRecord> reread;
  REQUIRE(reread.read(*written).has_value());
  REQUIRE(reread.records.size() == 3);
  for (const auto& original : table.records)
  {
    const auto found = std::ranges::find_if(
      reread.records, [&](const auto& r) { return r.id == original.id; });
    REQUIRE(found != reread.records.end());
    CHECK(*found == original);
  }
}

TEST_CASE("db: WDC3 write re-derives a copy table for duplicate-except-id rows",
          "[db]")
{
  // WdcRecord is wide (> 8 bytes), so rows identical in every field but their id
  // must be stored once plus a copy entry, not expanded.
  db::Table<WdcRecord> table;
  const WdcRecord base{.id = 10, .name = "Shared", .scale = 1.0f,
                       .flags = {4, 5, 6}, .bias = 3, .tags = {"a", "b"}};
  table.records.push_back(base);
  table.records.push_back(WdcRecord{.id = 20, .name = "Shared", .scale = 1.0f,
                                    .flags = {4, 5, 6}, .bias = 3, .tags = {"a", "b"}});
  table.records.push_back(WdcRecord{.id = 30, .name = "Unique", .scale = 2.0f,
                                    .flags = {7, 8, 9}, .bias = 1, .tags = {"c", "d"}});
  table.records.push_back(WdcRecord{.id = 40, .name = "Shared", .scale = 1.0f,
                                    .flags = {4, 5, 6}, .bias = 3, .tags = {"a", "b"}});

  const auto written = table.write();
  REQUIRE(written.has_value());

  db::wire::Wdc3Header header;
  std::memcpy(&header, written->data(), sizeof header);
  db::wire::Wdc3SectionHeader section;
  std::memcpy(&section, written->data() + sizeof header, sizeof section);
  // Two distinct rows are kept; ids 20 and 40 become copies of id 10.
  CHECK(header.record_count == 2);
  CHECK(section.copy_table_count == 2);

  db::Table<WdcRecord> reread;
  REQUIRE(reread.read(*written).has_value());
  REQUIRE(reread.records.size() == 4);
  // Every original row round-trips (copies re-expanded), id included.
  for (const WdcRecord& original : table.records)
  {
    const auto found = std::ranges::find_if(
      reread.records, [&](const WdcRecord& r) { return r.id == original.id; });
    REQUIRE(found != reread.records.end());
    CHECK(*found == original);
  }
}

TEST_CASE("db: WDC3 write bitpacks integer columns to their needed width", "[db]")
{
  db::Table<WdcSmallRecord> table;
  for (std::uint32_t i = 1; i <= 3; ++i)
    table.records.push_back(WdcSmallRecord{.id = static_cast<std::int32_t>(i), .value = i});

  const auto written = table.write();
  REQUIRE(written.has_value());

  db::wire::Wdc3Header header;
  std::memcpy(&header, written->data(), sizeof header);
  // Values 1..3 need 2 bits, not the declared 32 — a bitpacked record is 1 byte.
  CHECK(header.field_count == 1);        // the id is non-inline (id_list)
  CHECK(header.record_size == 1);

  db::Table<WdcSmallRecord> reread;
  REQUIRE(reread.read(*written).has_value());
  CHECK(reread.records == table.records);
}

namespace
{
  // A Cataclysm-era record: single already-localized string column (no
  // LocString), so its WDB2 record stride is small and easy to hand-build.
  struct CataRecord
  {
    static constexpr ClientVersion version = versions::cata;
    static constexpr std::string_view table_name = "CataUnitTest";

    [[=db::id]]
    std::uint32_t id = 0;

    std::string name;

    std::int32_t value = 0;

    bool operator==(const CataRecord&) const = default;
  };

  constexpr std::size_t cata_stride = 4 + 4 + 4;
  static_assert(db::record_stride<CataRecord>() == cata_stride);

  /// A WDB2 image with no id-index block (max_id == 0) and no copy table.
  std::vector<std::byte> make_wdb2_plain()
  {
    const std::string block{"\0Ironforge\0", 11};
    std::vector<std::byte> image;
    append(image, db::wire::wdb2_magic);
    append(image, std::uint32_t{2});                     // record_count
    append(image, db::field_slot_count<CataRecord>());   // field_count
    append(image, std::uint32_t{cata_stride});           // record_size
    append(image, static_cast<std::uint32_t>(block.size()));  // string_block_size
    append(image, std::uint32_t{0xABCD1234});            // table_hash
    append(image, std::uint32_t{15595});                 // build
    append(image, std::uint32_t{0});                     // timestamp
    append(image, std::uint32_t{0});                     // min_id
    append(image, std::uint32_t{0});                     // max_id -> no index block
    append(image, std::uint32_t{0});                     // locale
    append(image, std::uint32_t{0});                     // copy_table_size

    append(image, std::uint32_t{10});   // id
    append(image, std::uint32_t{1});    // name -> "Ironforge"
    append(image, std::int32_t{-1});    // value
    append(image, std::uint32_t{20});   // id
    append(image, std::uint32_t{0});    // name -> "" (offset 0)
    append(image, std::int32_t{99});    // value

    for (char c : block)
      image.push_back(static_cast<std::byte>(c));
    return image;
  }

  /// A WDB2 image WITH an id-index block (max_id != 0) and a copy table.
  std::vector<std::byte> make_wdb2_indexed()
  {
    const std::string block{"\0Ironforge\0", 11};
    const std::uint32_t min_id = 10, max_id = 11;  // 2 ids -> 2 index entries
    std::vector<std::byte> image;
    append(image, db::wire::wdb2_magic);
    append(image, std::uint32_t{2});
    append(image, db::field_slot_count<CataRecord>());
    append(image, std::uint32_t{cata_stride});
    append(image, static_cast<std::uint32_t>(block.size()));
    append(image, std::uint32_t{0xABCD1234});
    append(image, std::uint32_t{15595});
    append(image, std::uint32_t{0});
    append(image, min_id);
    append(image, max_id);
    append(image, std::uint32_t{0});
    append(image, std::uint32_t{8});  // copy_table_size -> one {id,id} pair

    // id-index block: int32 indices[2] then int16 string_lengths[2] (6B/id)
    append(image, std::int32_t{0});
    append(image, std::int32_t{1});
    append(image, std::int16_t{10});
    append(image, std::int16_t{1});

    append(image, std::uint32_t{10});
    append(image, std::uint32_t{1});
    append(image, std::int32_t{-1});
    append(image, std::uint32_t{11});
    append(image, std::uint32_t{0});
    append(image, std::int32_t{99});

    for (char c : block)
      image.push_back(static_cast<std::byte>(c));

    // copy table: {new_id, copied_id}
    append(image, std::uint32_t{12});
    append(image, std::uint32_t{10});
    return image;
  }
}

TEST_CASE("db: WDB2 without an index block decodes and round-trips", "[db]")
{
  const auto image = make_wdb2_plain();
  db::Table<CataRecord> table;
  REQUIRE(table.read(image).has_value());

  REQUIRE(table.records.size() == 2);
  CHECK(table.records[0].id == 10);
  CHECK(table.records[0].name == "Ironforge");
  CHECK(table.records[0].value == -1);
  CHECK(table.records[1].id == 20);
  CHECK(table.records[1].name.empty());
  CHECK(table.records[1].value == 99);

  const auto written = table.write();
  REQUIRE(written.has_value());
  REQUIRE(written->size() == image.size());
  CHECK(std::memcmp(written->data(), image.data(), image.size()) == 0);
}

TEST_CASE("db: WDB2 with index and copy blocks round-trips them verbatim", "[db]")
{
  const auto image = make_wdb2_indexed();
  db::Table<CataRecord> table;
  REQUIRE(table.read(image).has_value());

  REQUIRE(table.records.size() == 2);
  CHECK(table.records[0].name == "Ironforge");

  const auto written = table.write();
  REQUIRE(written.has_value());
  REQUIRE(written->size() == image.size());
  CHECK(std::memcmp(written->data(), image.data(), image.size()) == 0);
}

TEST_CASE("db: WDB2 in-place record edits re-encode; index rebuild rejected", "[db]")
{
  SECTION("editing an indexed table's record values keeps the index verbatim")
  {
    const auto image = make_wdb2_indexed();
    db::Table<CataRecord> table;
    REQUIRE(table.read(image).has_value());
    table.records[1].value = 4242;

    const auto written = table.write();
    REQUIRE(written.has_value());
    db::Table<CataRecord> reread;
    REQUIRE(reread.read(*written).has_value());
    CHECK(reread.records == table.records);
  }

  SECTION("adding a record to an indexed table errors (index cannot be rebuilt)")
  {
    const auto image = make_wdb2_indexed();
    db::Table<CataRecord> table;
    REQUIRE(table.read(image).has_value());
    table.records.push_back(CataRecord{.id = 30, .name = "Darnassus", .value = 7});

    const auto written = table.write();
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == ErrorCode::InvalidEntityState);
  }
}

TEST_CASE("db: a fresh Cata table writes WDB2, a fresh WotLK table writes WDBC",
          "[db]")
{
  db::Table<CataRecord> cata;
  cata.records.push_back(CataRecord{.id = 1, .name = "Stormwind", .value = 5});
  const auto cata_bytes = cata.write();
  REQUIRE(cata_bytes.has_value());
  REQUIRE(cata_bytes->size() >= 4);
  CHECK(std::memcmp(cata_bytes->data(), "WDB2", 4) == 0);

  db::wire::Wdb2Header header;
  std::memcpy(&header, cata_bytes->data(), sizeof header);
  CHECK(header.build == versions::cata.build);
  CHECK(header.record_count == 1);
  CHECK(header.max_id == 0);  // fresh tables emit no index block

  db::Table<CataRecord> reread;
  REQUIRE(reread.read(*cata_bytes).has_value());
  CHECK(reread.records == cata.records);

  db::Table<TestRecord> wotlk;  // TestRecord::version is wotlk
  wotlk.records.push_back(TestRecord{.id = 1, .name = "x"});
  const auto wotlk_bytes = wotlk.write();
  REQUIRE(wotlk_bytes.has_value());
  CHECK(std::memcmp(wotlk_bytes->data(), "WDBC", 4) == 0);
}
