#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/formats/chunk/fourcc.hpp>

using namespace wowlib::formats;

TEST_CASE("reversed four_cc equals the u32 memcpy'd from disk bytes", "[formats][fourcc]")
{
  // A WMO/ADT-style file stores 'MVER' as the bytes "REVM".
  const char disk[4]{'R', 'E', 'V', 'M'};
  std::uint32_t scanned = 0;
  std::memcpy(&scanned, disk, 4);
  CHECK(scanned == four_cc("MVER"));
}

TEST_CASE("forward four_cc equals the u32 memcpy'd from as-written bytes", "[formats][fourcc]")
{
  // A Legion+ M2 stores the chunk id 'AFID' as the bytes "AFID".
  const char disk[4]{'A', 'F', 'I', 'D'};
  std::uint32_t scanned = 0;
  std::memcpy(&scanned, disk, 4);
  CHECK(scanned == four_cc("AFID", FourCCEndian::forward));
}

TEST_CASE("four_cc values differ per endian and per code", "[formats][fourcc]")
{
  STATIC_CHECK(four_cc("MVER") != four_cc("MVER", FourCCEndian::forward));
  STATIC_CHECK(four_cc("MOHD") != four_cc("MOMT"));
}

TEST_CASE("fourcc_to_string round-trips both endians", "[formats][fourcc]")
{
  STATIC_CHECK(fourcc_to_string(four_cc("MOGP")) == "MOGP");
  STATIC_CHECK(fourcc_to_string(four_cc("SKID", FourCCEndian::forward), FourCCEndian::forward)
               == "SKID");
}
