#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/offset_file.hpp>
#include <wowlib/formats/common/version_slot.hpp>

using namespace wowlib;
using namespace wowlib::formats;

namespace
{
  // A version boundary for gated members, plus one version on each side.
  inline constexpr ClientVersion boundary{5, 0, 0, 0};
  inline constexpr ClientVersion old_v = versions::wotlk;
  inline constexpr ClientVersion new_v = versions::shadowlands;

  // M2Track-shaped nested record: inline scalars + per-sequence nested arrays.
  struct TestTrack
  {
    std::uint16_t interpolation = 0;
    std::uint16_t global_sequence = 0xFFFF;

    [[=formats::sequence_data]]
    std::vector<std::vector<std::uint32_t>> timestamps;

    [[=formats::sequence_data]]
    std::vector<std::vector<float>> values;

    bool operator==(const TestTrack&) const = default;
  };

  // A record mixing a raw value, a string, an inline nested record and an array.
  struct TestRecord
  {
    std::uint32_t id = 0;
    std::string name;
    TestTrack track{};
    std::vector<std::uint16_t> indices;

    bool operator==(const TestRecord&) const = default;
  };

  static_assert(detail::wire_size<TestTrack, old_v>() == 20);
  static_assert(detail::wire_size<TestRecord, old_v>() == 40);
  static_assert(detail::wire_size<std::vector<std::vector<float>>, old_v>() == 8);
  static_assert(detail::wire_size<std::string, old_v>() == 8);

  namespace traits
  {
    /** Pre-boundary-only members (conditionally inherited). */
    struct ModelOld
    {
      [[=until(boundary)]]
      std::vector<std::uint32_t> old_refs;

      bool operator==(const ModelOld&) const = default;
    };

    /** Post-boundary-only members (conditionally inherited). */
    struct ModelNew
    {
      [[=since(boundary)]]
      std::vector<std::uint64_t> new_refs;

      bool operator==(const ModelNew&) const = default;
    };
  }

  template <ClientVersion V>
  struct TestModel : OffsetFile<TestModel<V>>,
                     slot<V, ClientVersion{0, 0, 0, 0}, traits::ModelOld, boundary>,
                     slot<V, boundary, traits::ModelNew>
  {
    static constexpr ClientVersion version = V;

    // The authoritative wire order: interleaves the trait members between the
    // primary members, proving flatten order does not leak onto the wire.
    static constexpr std::array<std::string_view, 8> wire_order{
      "magic", "format_version", "global_flags", "name",
      "old_refs", "records", "new_refs", "combos"};

    std::uint32_t magic = 0x3244544D;  // 'MTD2'
    std::uint32_t format_version = 264;
    std::uint32_t global_flags = 0;
    std::string name;
    std::vector<TestRecord> records;

    [[=gated_by(0x8)]]
    std::vector<std::uint16_t> combos;

    bool operator==(const TestModel&) const = default;
  };

  using OldModel = TestModel<old_v>;
  using NewModel = TestModel<new_v>;

  static_assert(OffsetEntity<OldModel>);
  static_assert(OffsetEntity<NewModel>);
  // An offset entity slots straight into a chunk member (the MD21 path).
  static_assert(SelfSerializing<OldModel>);

  /** A model with every member kind populated. */
  template <ClientVersion V>
  TestModel<V> sample_model()
  {
    TestModel<V> m;
    m.name = "creature/test/test.mdx";
    TestRecord r0;
    r0.id = 7;
    r0.name = "first";
    r0.track.interpolation = 1;
    r0.track.timestamps = {{0, 100, 200}, {7, 8}, {33}};
    r0.track.values = {{0.f, 0.5f, 1.f}, {0.1f, 0.2f}, {0.25f}};
    r0.indices = {1, 2, 3, 4};
    TestRecord r1;
    r1.id = 8;
    r1.track.global_sequence = 2;
    m.records = {r0, r1};
    if constexpr (requires { m.old_refs; })
      m.old_refs = {10, 11, 12};
    if constexpr (requires { m.new_refs; })
      m.new_refs = {100, 200};
    return m;
  }

  template <typename E>
  E reread(std::span<const std::byte> bytes)
  {
    E out;
    auto r = out.read(bytes);
    INFO((r ? std::string{} : r.error().message));
    REQUIRE(r.has_value());
    return out;
  }

  template <typename T>
  concept has_new_refs = requires(T e) { e.new_refs; };
  template <typename T>
  concept has_old_refs = requires(T e) { e.old_refs; };

  std::uint32_t u32_at(std::span<const std::byte> bytes, std::size_t pos)
  {
    std::uint32_t v = 0;
    REQUIRE(pos + 4 <= bytes.size());
    std::memcpy(&v, bytes.data() + pos, 4);
    return v;
  }
}

TEST_CASE("offset entity round-trips all member kinds", "[formats][offset]")
{
  const auto m = sample_model<old_v>();
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  const auto back = reread<OldModel>(*bytes);
  CHECK(back == m);
}

TEST_CASE("wire_order interleaves trait members at their wire positions", "[formats][offset]")
{
  const auto m = sample_model<old_v>();
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  // image: magic(4) version(4) flags(4) name(8) old_refs(8) records(8) = 36
  // (combos gated off, new_refs inactive for old_v).
  CHECK(u32_at(*bytes, 0) == 0x3244544D);
  CHECK(u32_at(*bytes, 4) == 264);
  CHECK(u32_at(*bytes, 8) == 0);
  CHECK(u32_at(*bytes, 12) == m.name.size() + 1);  // name count includes NUL
  CHECK(u32_at(*bytes, 20) == 3);                  // old_refs count before records
  CHECK(u32_at(*bytes, 28) == 2);                  // records count
  // Every block offset is 16-byte aligned.
  for (std::size_t slot : {12u, 20u, 28u})
    CHECK(u32_at(*bytes, slot + 4) % 16 == 0);
}

TEST_CASE("version gating drops the other era's trait members", "[formats][offset]")
{
  const auto old_m = sample_model<old_v>();
  const auto new_m = sample_model<new_v>();
  auto old_bytes = old_m.write();
  auto new_bytes = new_m.write();
  REQUIRE(old_bytes.has_value());
  REQUIRE(new_bytes.has_value());

  const auto old_back = reread<OldModel>(*old_bytes);
  CHECK(old_back.old_refs == std::vector<std::uint32_t>{10, 11, 12});
  const auto new_back = reread<NewModel>(*new_bytes);
  CHECK(new_back.new_refs == std::vector<std::uint64_t>{100, 200});

  static_assert(!has_new_refs<OldModel>);
  static_assert(!has_old_refs<NewModel>);
  static_assert(has_old_refs<OldModel>);
  static_assert(has_new_refs<NewModel>);
}

TEST_CASE("gated members occupy wire bytes only when their flag is set", "[formats][offset]")
{
  auto gated_off = sample_model<old_v>();
  auto gated_on = sample_model<old_v>();
  gated_on.global_flags = 0x8;
  gated_on.combos = {5, 6, 7};

  auto off_bytes = gated_off.write();
  auto on_bytes = gated_on.write();
  REQUIRE(off_bytes.has_value());
  REQUIRE(on_bytes.has_value());

  const auto off_back = reread<OldModel>(*off_bytes);
  CHECK(off_back == gated_off);
  const auto on_back = reread<OldModel>(*on_bytes);
  CHECK(on_back == gated_on);
  CHECK(on_back.combos == std::vector<std::uint16_t>{5, 6, 7});
}

TEST_CASE("empty vectors and strings write valid slots", "[formats][offset]")
{
  OldModel m;  // everything default/empty
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  // name: empty string still counts its NUL (client buffer-size semantics)
  CHECK(u32_at(*bytes, 12) == 1);
  // old_refs / records: {0, 0}
  CHECK(u32_at(*bytes, 20) == 0);
  CHECK(u32_at(*bytes, 24) == 0);
  CHECK(u32_at(*bytes, 28) == 0);
  CHECK(u32_at(*bytes, 32) == 0);
  const auto back = reread<OldModel>(*bytes);
  CHECK(back == m);
}

TEST_CASE("out-of-bounds offsets are structural errors", "[formats][offset]")
{
  const auto m = sample_model<old_v>();
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  // Corrupt the records array offset to point past the buffer.
  const std::uint32_t bad = static_cast<std::uint32_t>(bytes->size() + 1);
  std::memcpy(bytes->data() + 32, &bad, 4);
  OldModel out;
  auto r = out.read(*bytes);
  REQUIRE(!r.has_value());
  CHECK(r.error().code == ErrorCode::OffsetOutOfBounds);

  // Truncating the image below the entity wire size fails, too.
  OldModel out2;
  auto r2 = out2.read(std::span{*bytes}.first(10));
  REQUIRE(!r2.has_value());
  CHECK(r2.error().code == ErrorCode::OffsetOutOfBounds);
}

TEST_CASE("sequence_data routes per-sequence blocks through the contexts", "[formats][offset]")
{
  auto m = sample_model<old_v>();

  // Route sequence 0 and 2 of every track to external per-sequence buffers,
  // as the M2 .anim split does; sequence 1 stays inline.
  std::map<std::size_t, FileBuffer> anim;
  OffsetWriteContext wctx;
  wctx.sequence_sink = [&](std::size_t i) -> FileBuffer* {
    if (i == 1)
      return nullptr;
    return &anim[i];
  };
  auto bytes = m.write(wctx);
  REQUIRE(bytes.has_value());
  CHECK(!anim[0].empty());  // sequence 0 holds timestamps + values data
  CHECK(anim.contains(2));

  // A context returning empty spans (missing .anim files) leaves those
  // sequences empty while the inline sequence still reads...
  OffsetReadContext missing;
  missing.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
    if (i == 1)
      return *bytes;
    return {};
  };
  OldModel partial;
  {
    auto r = partial.read(*bytes, missing);
    REQUIRE(r.has_value());
  }
  CHECK(partial.records[0].track.timestamps[0].empty());
  CHECK(partial.records[0].track.timestamps[1] == std::vector<std::uint32_t>{7, 8});
  CHECK(partial.records[0].track.values[2].empty());

  // ...while resolving through the read context restores the full entity.
  OffsetReadContext rctx;
  rctx.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
    if (i == 1)
      return *bytes;  // inline: resolves against the entity's own buffer
    return anim[i];
  };
  OldModel full;
  {
    auto r = full.read(*bytes, rctx);
    REQUIRE(r.has_value());
  }
  CHECK(full == m);
}

TEST_CASE("offset entity composes as a SelfSerializing chunk payload", "[formats][offset]")
{
  const auto m = sample_model<old_v>();
  FileBuffer out;
  REQUIRE(m.write(out).has_value());
  auto direct = m.write();
  REQUIRE(direct.has_value());
  CHECK(out == *direct);
  CHECK(!m.empty());
}
