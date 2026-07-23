#include <catch2/catch_test_macros.hpp>

#include <wowlib/formats/wmo/convert.hpp>

using namespace wowlib;
using namespace wowlib::formats;

TEST_CASE("WMO declares its supported version ladder", "[formats][convert]")
{
  STATIC_CHECK(supported_versions<wmo::WMO>.size() == 11);
  STATIC_CHECK(supported_versions<wmo::WMO>[0] == versions::vanilla);
  STATIC_CHECK(supported_versions<wmo::WMO>[2] == versions::wotlk);
  STATIC_CHECK(supported_versions<wmo::WMO>[10] == versions::tww);
}

TEST_CASE("identity conversion copies the entity", "[formats][convert]")
{
  wmo::WMO<versions::wotlk> source;
  source.root.header.n_groups = 3;
  source.root.materials.resize(2);
  source.root.materials[1].blend_mode = 4;

  const auto same = convert<versions::wotlk>(source);
  REQUIRE(same.has_value());
  CHECK(same->root.header.n_groups == 3);
  CHECK(same->root.materials.size() == 2);
  CHECK(same->root.materials[1].blend_mode == 4);
}

// A cross-version convert<versions::shadowlands>(wotlk_wmo) is a compile-time
// error until a convert_step for the pair is written — the static_assert names
// the missing overload. Verified manually; not expressible as a runtime test.
static_assert(!detail::HasConvertStep<wmo::WMO, versions::wotlk, versions::shadowlands>,
              "when the first real step lands, drop this assert and add tests");
