#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/error.hpp>
#include <wowlib/core/expansion.hpp>

using namespace wowlib;

TEST_CASE("expansions map onto the targeted release constants", "[expansion]")
{
  STATIC_CHECK(to_client_version(Expansion::Vanilla) == versions::vanilla);
  STATIC_CHECK(to_client_version(Expansion::Wotlk) == versions::wotlk);
  STATIC_CHECK(to_client_version(Expansion::Shadowlands) == versions::shadowlands);
  STATIC_CHECK(to_client_version(Expansion::TheWarWithin) == versions::tww);
}

TEST_CASE("exact version constants round-trip through to_expansion", "[expansion]")
{
  STATIC_CHECK(to_expansion(versions::wotlk) == Expansion::Wotlk);
  STATIC_CHECK(to_expansion(versions::shadowlands) == Expansion::Shadowlands);

  // an arbitrary non-targeted build is not an exact match
  STATIC_CHECK(!to_expansion(ClientVersion{3, 3, 0, 10958}).has_value());
}

TEST_CASE("expansion_of classifies any build by major version", "[expansion]")
{
  STATIC_CHECK(expansion_of(ClientVersion{3, 3, 0, 10958}) == Expansion::Wotlk);
  STATIC_CHECK(expansion_of(ClientVersion{9, 0, 1, 36216}) == Expansion::Shadowlands);
  STATIC_CHECK(!expansion_of(ClientVersion{99, 0, 0, 1}).has_value());
}

TEST_CASE("expansion names stringify via reflection", "[expansion][reflect]")
{
  STATIC_CHECK(enum_name(Expansion::Wotlk) == "Wotlk");
  CHECK(enum_name(Expansion::TheWarWithin) == "TheWarWithin");
}

TEST_CASE("new chunk error codes stringify", "[expansion][error]")
{
  STATIC_CHECK(to_string(ErrorCode::ChunkTruncated) == "ChunkTruncated");
  STATIC_CHECK(to_string(ErrorCode::UnsupportedClientVersion) == "UnsupportedClientVersion");
}
