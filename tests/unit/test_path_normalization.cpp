#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/path.hpp>

using wowlib::normalizePath;
using wowlib::toNativeRelative;

TEST_CASE("normalize_path canonicalizes case and separators", "[path]")
{
  CHECK(normalizePath("World/Maps/Azeroth/Azeroth.wdt") ==
        "world\\maps\\azeroth\\azeroth.wdt");
  CHECK(normalizePath("Interface\\GlueXML\\GlueStrings.lua") ==
        "interface\\gluexml\\gluestrings.lua");
  CHECK(normalizePath("MIXED/Style\\Path.BLP") == "mixed\\style\\path.blp");
}

TEST_CASE("normalize_path collapses and trims separators", "[path]")
{
  CHECK(normalizePath("//leading/slash") == "leading\\slash");
  CHECK(normalizePath("double//inner\\\\seps") == "double\\inner\\seps");
  CHECK(normalizePath("trailing/slash/") == "trailing\\slash");
  CHECK(normalizePath("") == "");
}

TEST_CASE("normalization is idempotent", "[path]")
{
  const auto once = normalizePath("World/Maps/Azeroth/Azeroth.wdt");
  CHECK(normalizePath(once) == once);
}

TEST_CASE("to_native_relative round-trips canonical form", "[path]")
{
  const auto canonical = normalizePath("World/Maps/Azeroth/Azeroth.wdt");
  CHECK(toNativeRelative(canonical) == "world/maps/azeroth/azeroth.wdt");
  CHECK(normalizePath(toNativeRelative(canonical)) == canonical);
}