#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <wowlib/formats/common/fourcc.hpp>

using namespace wowlib::formats;

TEST_CASE("reversed four_cc equals the u32 memcpy'd from disk bytes", "[formats][fourcc]")
{
  // A WMO/ADT-style file stores 'MVER' as the bytes "REVM".
  const char disk[4]{'R', 'E', 'V', 'M'};
  std::uint32_t scanned = 0;
  std::memcpy(&scanned, disk, 4);
  CHECK(scanned == fourCc("MVER"));
}

TEST_CASE("forward four_cc equals the u32 memcpy'd from as-written bytes", "[formats][fourcc]")
{
  // A Legion+ M2 stores the chunk id 'AFID' as the bytes "AFID".
  const char disk[4]{'A', 'F', 'I', 'D'};
  std::uint32_t scanned = 0;
  std::memcpy(&scanned, disk, 4);
  CHECK(scanned == fourCc("AFID", FourCCEndian::Forward));
}

TEST_CASE("four_cc values differ per endian and per code", "[formats][fourcc]")
{
  STATIC_CHECK(fourCc("MVER") != fourCc("MVER", FourCCEndian::Forward));
  STATIC_CHECK(fourCc("MOHD") != fourCc("MOMT"));
}

TEST_CASE("fourcc_to_string round-trips both endians", "[formats][fourcc]")
{
  STATIC_CHECK(fourccToString(fourCc("MOGP")) == "MOGP");
  STATIC_CHECK(fourccToString(fourCc("SKID", FourCCEndian::Forward), FourCCEndian::Forward)
               == "SKID");
}
