#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <span>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunked_file.hpp>

using namespace wowlib;
using namespace wowlib::formats;

namespace {
  // A version boundary for since/until members, plus one version on each side.
  inline constexpr ClientVersion Boundary{8, 1, 0, 28186};
  inline constexpr ClientVersion OldV = versions::Wotlk;
  inline constexpr ClientVersion NewV = versions::Shadowlands;

  struct InnerHeader {
    std::uint32_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
  };

  static_assert(sizeof(InnerHeader) == 8);

  template <ClientVersion V>
  struct Body : ChunkedFile<Body<V>> {
    static constexpr ClientVersion Version = V;

    [[=formats::Header]] InnerHeader head{};
    [[=chunk("IVEC")]] std::vector<std::uint32_t> values;
    [[=chunk("IOPT"), =formats::Optional]] std::vector<std::uint16_t> extra;
  };

  template <ClientVersion V>
  struct TestEntity : ChunkedFile<TestEntity<V>> {
    static constexpr ClientVersion Version = V;

    [[=chunk("TVER")]] std::uint32_t ver = 1;
    [[=chunk("TSTR"), =formats::Optional]] StringBlock names;
    [[=chunk("TVEC")]] std::vector<std::uint64_t> data;
    [[=chunk("TREP"), =formats::Optional, =repeats(3)]] Repeated<std::vector<std::uint16_t>, 3>
    sets;
    [[=chunk("TOLD"), =until(Boundary), =formats::Optional]] std::vector<std::uint32_t> oldRefs;
    [[=chunk("TNEW"), =since(Boundary), =formats::Optional]] std::vector<std::uint32_t> newRefs;
    [[=chunk("TCON"), =formats::Container, =formats::Optional]] Body<V> body{};
  };

  static_assert(ChunkedEntity<TestEntity<OldV>>);
  static_assert(ChunkedEntity<Body<NewV>>);
  static_assert(SelfSerializing<StringBlock>);
  static_assert(SelfSerializing<ChunkBlob>);
  static_assert(!SelfSerializing<std::vector<std::uint32_t>>);

  /** read() into a fresh E, unwrapping like the old free-function helper. */
  template <typename E>
  Result<E> readFresh(std::span<const std::byte> data) {
    E entity{};
    if (auto r = entity.read(data); !r)
      return std::unexpected{r.error()};
    return entity;
  }

  // --- synthetic buffer building ---------------------------------------------

  void putBytes(FileBuffer& b, const void* p, std::size_t n) {
    const auto* bytes = static_cast<const std::byte*>(p);
    b.insert(b.end(), bytes, bytes + n);
  }

  void putChunk(FileBuffer& b, const char (&cc)[5], std::span<const std::byte> payload) {
    const std::uint32_t fourccMagic = fourcc(cc);
    const auto size = static_cast<std::uint32_t>(payload.size());
    putBytes(b, &fourccMagic, sizeof fourccMagic);
    putBytes(b, &size, sizeof size);
    putBytes(b, payload.data(), payload.size());
  }

  template <typename T>
  void putPodChunk(FileBuffer& b, const char (&cc)[5], const T& v) {
    putChunk(b, cc, std::span{reinterpret_cast<const std::byte*>(&v), sizeof v});
  }

  template <typename T>
  void putVecChunk(FileBuffer& b, const char (&cc)[5], const std::vector<T>& v) {
    putChunk(b, cc,
             std::span{reinterpret_cast<const std::byte*>(v.data()), v.size() * sizeof(T)});
  }

  FileBuffer bodyBytes(std::uint32_t a, const std::vector<std::uint32_t>& values) {
    FileBuffer b;
    const InnerHeader head{a, 7, 9};
    putBytes(b, &head, sizeof head);
    putVecChunk(b, "IVEC", values);
    return b;
  }

  /** The canonical little file: every required chunk plus a few extras. */
  FileBuffer canonicalFile() {
    FileBuffer b;
    putPodChunk(b, "TVER", std::uint32_t{17});
    putVecChunk(b, "TVEC", std::vector<std::uint64_t>{10, 20, 30});
    putChunk(b, "TSTR", std::span{reinterpret_cast<const std::byte*>("abc\0de\0"), 7});
    return b;
  }
}

TEST_CASE("reads are chunk-order independent", "[formats][chunk]") {
  FileBuffer shuffled;
  putChunk(shuffled, "TSTR", std::span{reinterpret_cast<const std::byte*>("abc\0de\0"), 7});
  putVecChunk(shuffled, "TVEC", std::vector<std::uint64_t>{10, 20, 30});
  putPodChunk(shuffled, "TVER", std::uint32_t{17});

  const auto canonical = readFresh<TestEntity<OldV>>(canonicalFile());
  const auto reordered = readFresh<TestEntity<OldV>>(shuffled);
  REQUIRE(canonical.has_value());
  REQUIRE(reordered.has_value());

  CHECK(canonical->ver == 17);
  CHECK(reordered->ver == 17);
  CHECK(canonical->data == reordered->data);
  REQUIRE(canonical->names.entries().size() == 2);
  CHECK(canonical->names.entries()[0].value == reordered->names.entries()[0].value);
  CHECK(canonical->names.at(4) == "de");
}

TEST_CASE("journal replay reproduces the original bytes exactly", "[formats][chunk]") {
  FileBuffer shuffled;
  putVecChunk(shuffled, "TVEC", std::vector<std::uint64_t>{1, 2});
  putPodChunk(shuffled, "TVER", std::uint32_t{3});

  const auto entity = readFresh<TestEntity<OldV>>(shuffled);
  REQUIRE(entity.has_value());
  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == shuffled); // shuffled order preserved, not canonicalized
}

TEST_CASE("string blocks decode entries and rebuild padded blobs exactly",
          "[formats][chunk]") {
  // padding zeros before, between and after entries, plus an unterminated tail
  const char raw[]{'\0', '\0', 'a', 'b', 'c', '\0', '\0', '\0', 'd', 'e', '\0', 'x', 'y'};
  FileBuffer file;
  putPodChunk(file, "TVER", std::uint32_t{1});
  putVecChunk(file, "TVEC", std::vector<std::uint64_t>{});
  putChunk(file, "TSTR", std::span{reinterpret_cast<const std::byte*>(raw), sizeof raw});

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());
  const StringBlock& names = entity->names;

  REQUIRE(names.entries().size() == 3);
  CHECK(names.size() == sizeof raw);
  CHECK(names.entries()[0].offset == 2);
  CHECK(names.entries()[0].value == "abc");
  CHECK(names.entries()[2].offset == 11);
  CHECK(names.entries()[2].value == "xy");

  // offset lookups: starts, suffixes, padding and out-of-range
  CHECK(names.at(2) == "abc");
  CHECK(names.at(4) == "c"); // mid-entry: suffix
  CHECK(names.at(0).empty()); // padding
  CHECK(names.at(5).empty()); // terminator
  CHECK(names.at(100).empty());

  // blobifying reproduces the original padding byte for byte
  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);
}

TEST_CASE("string block additions append past the blob end", "[formats][chunk]") {
  StringBlock block;
  CHECK(block.empty());
  const auto first = block.add("textures/stone.blp");
  const auto second = block.add("b.blp");
  CHECK(first == 0);
  CHECK(second == 19); // strlen + terminator
  CHECK(block.size() == 25);
  CHECK(block.at(first) == "textures/stone.blp");
  CHECK(block.at(second) == "b.blp");

  FileBuffer blob;
  REQUIRE(block.write(blob).has_value());
  CHECK(blob.size() == block.size());
  CHECK(std::memcmp(blob.data(), "textures/stone.blp\0b.blp\0", 25) == 0);
}

TEST_CASE("unknown chunks are preserved verbatim and replayed in position", "[formats][chunk]") {
  FileBuffer file;
  putPodChunk(file, "TVER", std::uint32_t{1});
  putVecChunk(file, "WERD", std::vector<std::uint32_t>{0xDEAD, 0xBEEF});
  putVecChunk(file, "TVEC", std::vector<std::uint64_t>{5});

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());
  REQUIRE(entity->unknown.size() == 1);
  CHECK(entity->unknown[0].fourcc == fourcc("WERD"));
  CHECK(entity->unknown[0].bytes.size() == 8);

  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);
}

TEST_CASE("repeated chunks fill slots in order and round-trip", "[formats][chunk]") {
  FileBuffer file;
  putPodChunk(file, "TVER", std::uint32_t{1});
  putVecChunk(file, "TREP", std::vector<std::uint16_t>{1});
  putVecChunk(file, "TVEC", std::vector<std::uint64_t>{5}); // interleaved
  putVecChunk(file, "TREP", std::vector<std::uint16_t>{2, 2});
  putVecChunk(file, "TREP", std::vector<std::uint16_t>{3, 3, 3});

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());
  REQUIRE(entity->sets.size() == 3);
  CHECK(entity->sets[0] == std::vector<std::uint16_t>{1});
  CHECK(entity->sets[2] == std::vector<std::uint16_t>{3, 3, 3});

  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);
}

TEST_CASE("a repeated chunk beyond capacity is preserved as unknown", "[formats][chunk]") {
  FileBuffer file;
  putPodChunk(file, "TVER", std::uint32_t{1});
  putVecChunk(file, "TVEC", std::vector<std::uint64_t>{});
  for (std::uint16_t i = 0; i < 4; ++i)
    putVecChunk(file, "TREP", std::vector<std::uint16_t>{i});

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());
  CHECK(entity->sets.size() == 3);
  REQUIRE(entity->unknown.size() == 1);
  CHECK(entity->unknown[0].fourcc == fourcc("TREP"));

  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);
}

TEST_CASE("duplicate non-repeated chunks are preserved as unknown", "[formats][chunk]") {
  FileBuffer file;
  putPodChunk(file, "TVER", std::uint32_t{1});
  putVecChunk(file, "TVEC", std::vector<std::uint64_t>{5});
  putPodChunk(file, "TVER", std::uint32_t{99}); // duplicate

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());
  CHECK(entity->ver == 1); // first occurrence wins
  REQUIRE(entity->unknown.size() == 1);

  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);
}

TEST_CASE("trailing stray bytes are preserved", "[formats][chunk]") {
  FileBuffer file = canonicalFile();
  const char stray[3]{'x', 'y', 'z'};
  putBytes(file, stray, 3);

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());
  CHECK(entity->trailing.size() == 3);

  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);
}

TEST_CASE("a chunk size overrunning the buffer is ChunkTruncated", "[formats][chunk]") {
  FileBuffer file;
  const std::uint32_t fourccMagic = fourcc("TVER");
  const std::uint32_t size = 1000; // way past the end
  putBytes(file, &fourccMagic, 4);
  putBytes(file, &size, 4);

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE_FALSE(entity.has_value());
  CHECK(entity.error().code == ErrorCode::ChunkTruncated);
}

TEST_CASE("size mismatches are diagnosed", "[formats][chunk]") {
  SECTION("data chunk with the wrong size") {
    FileBuffer file;
    const std::uint16_t half = 17;
    putPodChunk(file, "TVER", half); // 2 bytes, needs 4
    putVecChunk(file, "TVEC", std::vector<std::uint64_t>{});
    const auto entity = readFresh<TestEntity<OldV>>(file);
    REQUIRE_FALSE(entity.has_value());
    CHECK(entity.error().code == ErrorCode::ChunkSizeMismatch);
  }
  SECTION("array chunk not divisible by the element size") {
    FileBuffer file;
    putPodChunk(file, "TVER", std::uint32_t{1});
    const char odd[3]{1, 2, 3};
    putChunk(file, "TVEC", std::span{reinterpret_cast<const std::byte*>(odd), 3});
    const auto entity = readFresh<TestEntity<OldV>>(file);
    REQUIRE_FALSE(entity.has_value());
    CHECK(entity.error().code == ErrorCode::ChunkSizeMismatch);
  }
}

TEST_CASE("a missing required chunk is ChunkMissing", "[formats][chunk]") {
  FileBuffer file;
  putPodChunk(file, "TVER", std::uint32_t{1}); // no TVEC

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE_FALSE(entity.has_value());
  CHECK(entity.error().code == ErrorCode::ChunkMissing);
  CHECK(entity.error().message.contains("TVEC"));
}

TEST_CASE("since/until members follow the entity version", "[formats][chunk]") {
  FileBuffer file;
  putPodChunk(file, "TVER", std::uint32_t{1});
  putVecChunk(file, "TVEC", std::vector<std::uint64_t>{});
  putVecChunk(file, "TOLD", std::vector<std::uint32_t>{111});
  putVecChunk(file, "TNEW", std::vector<std::uint32_t>{222});

  SECTION("pre-boundary version reads TOLD, banks TNEW as unknown") {
    const auto entity = readFresh<TestEntity<OldV>>(file);
    REQUIRE(entity.has_value());
    CHECK(entity->oldRefs == std::vector<std::uint32_t>{111});
    CHECK(entity->newRefs.empty());
    REQUIRE(entity->unknown.size() == 1);
    CHECK(entity->unknown[0].fourcc == fourcc("TNEW"));
    CHECK(*entity->write() == file); // still byte-perfect
  }
  SECTION("post-boundary version reads TNEW, banks TOLD as unknown") {
    const auto entity = readFresh<TestEntity<NewV>>(file);
    REQUIRE(entity.has_value());
    CHECK(entity->newRefs == std::vector<std::uint32_t>{222});
    CHECK(entity->oldRefs.empty());
    REQUIRE(entity->unknown.size() == 1);
    CHECK(entity->unknown[0].fourcc == fourcc("TOLD"));
    CHECK(*entity->write() == file);
  }
}

TEST_CASE("container chunks nest with a header prelude", "[formats][chunk]") {
  FileBuffer file = canonicalFile();
  putChunk(file, "TCON", bodyBytes(42, {7, 8}));

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());
  CHECK(entity->body.head.a == 42);
  CHECK(entity->body.head.b == 7);
  CHECK(entity->body.values == std::vector<std::uint32_t>{7, 8});

  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());
  CHECK(*rewritten == file);
}

TEST_CASE("container inner errors surface", "[formats][chunk]") {
  FileBuffer inner; // header prelude only, missing required IVEC
  const InnerHeader head{1, 2, 3};
  putBytes(inner, &head, sizeof head);

  FileBuffer file = canonicalFile();
  putChunk(file, "TCON", inner);

  const auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE_FALSE(entity.has_value());
  CHECK(entity.error().code == ErrorCode::ChunkMissing);
  CHECK(entity.error().message.contains("IVEC"));
}

TEST_CASE("fresh entities write active members in declaration order", "[formats][chunk]") {
  TestEntity<OldV> entity;
  entity.ver = 5;
  entity.data = {1, 2, 3};
  entity.oldRefs = {9}; // active on OldV
  entity.newRefs = {8}; // INACTIVE on OldV: must not be written

  const auto out = entity.write();
  REQUIRE(out.has_value());

  FileBuffer expected;
  putPodChunk(expected, "TVER", std::uint32_t{5});
  putVecChunk(expected, "TVEC", std::vector<std::uint64_t>{1, 2, 3});
  putVecChunk(expected, "TOLD", std::vector<std::uint32_t>{9});
  CHECK(*out == expected);
}

TEST_CASE("members engaged after reading append after the journal", "[formats][chunk]") {
  const FileBuffer file = canonicalFile();
  auto entity = readFresh<TestEntity<OldV>>(file);
  REQUIRE(entity.has_value());

  entity->oldRefs = {4, 5}; // newly engaged, was not in the file
  const auto rewritten = entity->write();
  REQUIRE(rewritten.has_value());

  FileBuffer expected = file;
  putVecChunk(expected, "TOLD", std::vector<std::uint32_t>{4, 5});
  CHECK(*rewritten == expected);
}
