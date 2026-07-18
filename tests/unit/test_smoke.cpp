#include <catch2/catch_test_macros.hpp>

#include <wowlib/wowlib.hpp>

TEST_CASE("umbrella header compiles and core types are usable", "[smoke]")
{
  const auto key = wowlib::FileKey::by_both("World/Maps/Azeroth/Azeroth.wdt",
                                            wowlib::FileDataID{775971});
  REQUIRE(key.path == "world\\maps\\azeroth\\azeroth.wdt");
  REQUIRE(key.fdid == wowlib::FileDataID{775971});
}