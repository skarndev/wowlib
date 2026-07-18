#include <catch2/catch_test_macros.hpp>

#include <wowlib/fs/fdid_allocator.hpp>

using namespace wowlib;
using wowlib::fs::detail::FdidAllocator;

TEST_CASE("allocation is monotonic from the configured start", "[fdid]")
{
  FdidAllocator alloc{FileDataID{5000}};
  CHECK(alloc.next().value() == FileDataID{5000});
  CHECK(alloc.next().value() == FileDataID{5001});
  CHECK(alloc.peek() == FileDataID{5002});
}

TEST_CASE("note_existing bumps past previously used ids", "[fdid]")
{
  FdidAllocator alloc{FileDataID{5000}};
  alloc.note_existing(FileDataID{7000});
  CHECK(alloc.next().value() == FileDataID{7001});

  // ids below the cursor change nothing
  alloc.note_existing(FileDataID{42});
  CHECK(alloc.next().value() == FileDataID{7002});
}

TEST_CASE("the space exhausts at max, not before", "[fdid]")
{
  FdidAllocator alloc{FileDataID{0xFFFFFFFD}, FileDataID{0xFFFFFFFE}};
  CHECK(alloc.next().value() == FileDataID{0xFFFFFFFD});
  CHECK(alloc.next().value() == FileDataID{0xFFFFFFFE});

  const auto exhausted = alloc.next();
  REQUIRE_FALSE(exhausted.has_value());
  CHECK(exhausted.error().code == ErrorCode::FdidSpaceExhausted);
}