#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/path.hpp>

using wowlib::normalize_path;
using wowlib::to_native_relative;

TEST_CASE("normalize_path canonicalizes case and separators", "[path]")
{
  CHECK(normalize_path("World/Maps/Azeroth/Azeroth.wdt") ==
        "world\\maps\\azeroth\\azeroth.wdt");
  CHECK(normalize_path("Interface\\GlueXML\\GlueStrings.lua") ==
        "interface\\gluexml\\gluestrings.lua");
  CHECK(normalize_path("MIXED/Style\\Path.BLP") == "mixed\\style\\path.blp");
}

TEST_CASE("normalize_path collapses and trims separators", "[path]")
{
  CHECK(normalize_path("//leading/slash") == "leading\\slash");
  CHECK(normalize_path("double//inner\\\\seps") == "double\\inner\\seps");
  CHECK(normalize_path("trailing/slash/") == "trailing\\slash");
  CHECK(normalize_path("") == "");
}

TEST_CASE("normalization is idempotent", "[path]")
{
  const auto once = normalize_path("World/Maps/Azeroth/Azeroth.wdt");
  CHECK(normalize_path(once) == once);
}

TEST_CASE("to_native_relative round-trips canonical form", "[path]")
{
  const auto canonical = normalize_path("World/Maps/Azeroth/Azeroth.wdt");
  CHECK(to_native_relative(canonical) == "world/maps/azeroth/azeroth.wdt");
  CHECK(normalize_path(to_native_relative(canonical)) == canonical);
}