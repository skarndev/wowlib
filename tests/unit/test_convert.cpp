#include <catch2/catch_test_macros.hpp>

#include <wowlib/formats/wmo/convert.hpp>

using namespace wowlib;
using namespace wowlib::formats;

TEST_CASE("WMO declares its supported version ladder", "[formats][convert]")
{
  STATIC_CHECK(SupportedVersions<wmo::WMO>.size() == 11);
  STATIC_CHECK(SupportedVersions<wmo::WMO>[0] == versions::Vanilla);
  STATIC_CHECK(SupportedVersions<wmo::WMO>[2] == versions::Wotlk);
  STATIC_CHECK(SupportedVersions<wmo::WMO>[10] == versions::Tww);
}

TEST_CASE("identity conversion copies the entity", "[formats][convert]")
{
  wmo::WMO<versions::Wotlk> source;
  source.root.header.nGroups = 3;
  source.root.materials.resize(2);
  source.root.materials[1].blendMode = 4;

  const auto same = convert<versions::Wotlk>(source);
  REQUIRE(same.has_value());
  CHECK(same->root.header.nGroups == 3);
  CHECK(same->root.materials.size() == 2);
  CHECK(same->root.materials[1].blendMode == 4);
}

// A cross-version convert<versions::shadowlands>(wotlk_wmo) is a compile-time
// error until a convert_step for the pair is written — the static_assert names
// the missing overload. Verified manually; not expressible as a runtime test.
static_assert(!formats::detail::HasConvertStep<wmo::WMO, versions::Wotlk,
                                              versions::Shadowlands>,
              "when the first real step lands, drop this assert and add tests");
