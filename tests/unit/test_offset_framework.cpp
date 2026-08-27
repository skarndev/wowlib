#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/m2/offset_block.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::m2;

namespace
{
  // A version boundary for gated members, plus one version on each side.
  inline constexpr ClientVersion Boundary{5, 0, 0, 0};
  inline constexpr ClientVersion OldV = versions::Wotlk;
  inline constexpr ClientVersion NewV = versions::Shadowlands;

  // M2Track-shaped nested record: inline scalars + per-sequence nested arrays.
  struct TestTrack
  {
    std::uint16_t interpolation = 0;
    std::uint16_t globalSequence = 0xFFFF;

    [[=formats::SequenceData]]
    std::vector<std::vector<std::uint32_t>> timestamps;

    [[=formats::SequenceData]]
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

  static_assert(layoutSize<TestTrack, OldV>() == 20);
  static_assert(layoutSize<TestRecord, OldV>() == 40);
  static_assert(layoutSize<std::vector<std::vector<float>>, OldV>() == 8);
  static_assert(layoutSize<std::string, OldV>() == 8);

  namespace traits
  {
    /** Pre-boundary-only members (conditionally inherited). */
    struct ModelOld
    {
      [[=until(Boundary), =offsetAfter("name")]]
      std::vector<std::uint32_t> oldRefs;

      bool operator==(const ModelOld&) const = default;
    };

    /** Post-boundary-only members (conditionally inherited). */
    struct ModelNew
    {
      [[=since(Boundary), =offsetAfter("records")]]
      std::vector<std::uint64_t> newRefs;

      bool operator==(const ModelNew&) const = default;
    };
  }

  template <ClientVersion V>
  struct TestModel : M2OffsetBlock<TestModel<V>>,
                     Slot<V, ClientVersion{0, 0, 0, 0}, traits::ModelOld, Boundary>,
                     Slot<V, Boundary, traits::ModelNew>
  {
    static constexpr ClientVersion Version = V;

    // Layout order = own declaration order; the trait members' offsetAfter
    // anchors interleave them (oldRefs after name, newRefs after records),
    // proving flatten order does not leak onto the layout.
    std::uint32_t magic = 0x3244544D;  // 'MTD2'
    std::uint32_t formatVersion = 264;
    std::uint32_t globalFlags = 0;
    std::string name;
    std::vector<TestRecord> records;

    [[=gatedBy(0x8)]]
    std::vector<std::uint16_t> combos;

    bool operator==(const TestModel&) const = default;
  };

  using OldModel = TestModel<OldV>;
  using NewModel = TestModel<NewV>;

  static_assert(OffsetEntity<OldModel>);
  static_assert(OffsetEntity<NewModel>);
  // An offset entity slots straight into a chunk member (the MD21 path).
  static_assert(SelfSerializing<OldModel>);

  /** A model with every member kind populated. */
  template <ClientVersion V>
  TestModel<V> sampleModel()
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
    r1.track.globalSequence = 2;
    m.records = {r0, r1};
    if constexpr (requires { m.oldRefs; })
      m.oldRefs = {10, 11, 12};
    if constexpr (requires { m.newRefs; })
      m.newRefs = {100, 200};
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
  concept HasNewRefs = requires(T e) { e.newRefs; };
  template <typename T>
  concept HasOldRefs = requires(T e) { e.oldRefs; };

  std::uint32_t u32At(std::span<const std::byte> bytes, std::size_t pos)
  {
    std::uint32_t v = 0;
    REQUIRE(pos + 4 <= bytes.size());
    std::memcpy(&v, bytes.data() + pos, 4);
    return v;
  }
}

TEST_CASE("offset entity round-trips all member kinds", "[formats][offset]")
{
  const auto m = sampleModel<OldV>();
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  const auto back = reread<OldModel>(*bytes);
  CHECK(back == m);
}

TEST_CASE("offset_after anchors interleave trait members at their layout positions",
          "[formats][offset]")
{
  const auto m = sampleModel<OldV>();
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  // image: magic(4) version(4) flags(4) name(8) oldRefs(8) records(8) = 36
  // (combos gated off, newRefs inactive for OldV).
  CHECK(u32At(*bytes, 0) == 0x3244544D);
  CHECK(u32At(*bytes, 4) == 264);
  CHECK(u32At(*bytes, 8) == 0);
  CHECK(u32At(*bytes, 12) == m.name.size() + 1);  // name count includes NUL
  CHECK(u32At(*bytes, 20) == 3);                  // oldRefs count before records
  CHECK(u32At(*bytes, 28) == 2);                  // records count
  // Every block offset is 16-byte aligned.
  for (std::size_t slot : {12u, 20u, 28u})
    CHECK(u32At(*bytes, slot + 4) % 16 == 0);
}

TEST_CASE("version gating drops the other era's trait members", "[formats][offset]")
{
  const auto oldM = sampleModel<OldV>();
  const auto newM = sampleModel<NewV>();
  auto oldBytes = oldM.write();
  auto newBytes = newM.write();
  REQUIRE(oldBytes.has_value());
  REQUIRE(newBytes.has_value());

  const auto oldBack = reread<OldModel>(*oldBytes);
  CHECK(oldBack.oldRefs == std::vector<std::uint32_t>{10, 11, 12});
  const auto newBack = reread<NewModel>(*newBytes);
  CHECK(newBack.newRefs == std::vector<std::uint64_t>{100, 200});

  static_assert(!HasNewRefs<OldModel>);
  static_assert(!HasOldRefs<NewModel>);
  static_assert(HasOldRefs<OldModel>);
  static_assert(HasNewRefs<NewModel>);
}

TEST_CASE("gated members occupy layout bytes only when their flag is set", "[formats][offset]")
{
  auto gatedOff = sampleModel<OldV>();
  auto gatedOn = sampleModel<OldV>();
  gatedOn.globalFlags = 0x8;
  gatedOn.combos = {5, 6, 7};

  auto offBytes = gatedOff.write();
  auto onBytes = gatedOn.write();
  REQUIRE(offBytes.has_value());
  REQUIRE(onBytes.has_value());

  const auto offBack = reread<OldModel>(*offBytes);
  CHECK(offBack == gatedOff);
  const auto onBack = reread<OldModel>(*onBytes);
  CHECK(onBack == gatedOn);
  CHECK(onBack.combos == std::vector<std::uint16_t>{5, 6, 7});
}

TEST_CASE("empty vectors and strings write valid slots", "[formats][offset]")
{
  OldModel m;  // everything default/empty
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  // name: empty string still counts its NUL (client buffer-size semantics)
  CHECK(u32At(*bytes, 12) == 1);
  // oldRefs / records: {0, 0}
  CHECK(u32At(*bytes, 20) == 0);
  CHECK(u32At(*bytes, 24) == 0);
  CHECK(u32At(*bytes, 28) == 0);
  CHECK(u32At(*bytes, 32) == 0);
  const auto back = reread<OldModel>(*bytes);
  CHECK(back == m);
}

TEST_CASE("out-of-bounds offsets are structural errors", "[formats][offset]")
{
  const auto m = sampleModel<OldV>();
  auto bytes = m.write();
  REQUIRE(bytes.has_value());
  // Corrupt the records array offset to point past the buffer.
  const std::uint32_t bad = static_cast<std::uint32_t>(bytes->size() + 1);
  std::memcpy(bytes->data() + 32, &bad, 4);
  OldModel out;
  auto r = out.read(*bytes);
  REQUIRE(!r.has_value());
  CHECK(r.error().code == ErrorCode::OffsetOutOfBounds);

  // Truncating the image below the entity image size fails, too.
  OldModel out2;
  auto r2 = out2.read(std::span{*bytes}.first(10));
  REQUIRE(!r2.has_value());
  CHECK(r2.error().code == ErrorCode::OffsetOutOfBounds);
}

TEST_CASE("sequence_data routes per-sequence blocks through the contexts", "[formats][offset]")
{
  auto m = sampleModel<OldV>();

  // Route sequence 0 and 2 of every track to external per-sequence buffers,
  // as the M2 .anim split does; sequence 1 stays inline.
  std::map<std::size_t, FileBuffer> anim;
  OffsetWriteContext wctx;
  wctx.sequenceSink = [&](std::size_t i) -> FileBuffer* {
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
  missing.sequenceBase = [&](std::size_t i) -> std::span<const std::byte> {
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
  rctx.sequenceBase = [&](std::size_t i) -> std::span<const std::byte> {
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
  const auto m = sampleModel<OldV>();
  FileBuffer out;
  REQUIRE(m.write(out).has_value());
  auto direct = m.write();
  REQUIRE(direct.has_value());
  CHECK(out == *direct);
  CHECK(!m.empty());
}
