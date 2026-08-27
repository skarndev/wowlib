#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/error.hpp>
#include <wowlib/core/expansion.hpp>

using namespace wowlib;

TEST_CASE("expansions map onto the targeted release constants", "[expansion]")
{
  STATIC_CHECK(toClientVersion(Expansion::Vanilla) == versions::Vanilla);
  STATIC_CHECK(toClientVersion(Expansion::Wotlk) == versions::Wotlk);
  STATIC_CHECK(toClientVersion(Expansion::Shadowlands) == versions::Shadowlands);
  STATIC_CHECK(toClientVersion(Expansion::TheWarWithin) == versions::Tww);
}

TEST_CASE("exact version constants round-trip through to_expansion", "[expansion]")
{
  STATIC_CHECK(toExpansion(versions::Wotlk) == Expansion::Wotlk);
  STATIC_CHECK(toExpansion(versions::Shadowlands) == Expansion::Shadowlands);

  // an arbitrary non-targeted build is not an exact match
  STATIC_CHECK(!toExpansion(ClientVersion{3, 3, 0, 10958}).has_value());
}

TEST_CASE("expansion_of classifies any build by major version", "[expansion]")
{
  STATIC_CHECK(expansionOf(ClientVersion{3, 3, 0, 10958}) == Expansion::Wotlk);
  STATIC_CHECK(expansionOf(ClientVersion{9, 0, 1, 36216}) == Expansion::Shadowlands);
  STATIC_CHECK(!expansionOf(ClientVersion{99, 0, 0, 1}).has_value());
}

TEST_CASE("expansion names stringify via reflection", "[expansion][reflect]")
{
  STATIC_CHECK(enumName(Expansion::Wotlk) == "Wotlk");
  CHECK(enumName(Expansion::TheWarWithin) == "TheWarWithin");
}

TEST_CASE("new chunk error codes stringify", "[expansion][error]")
{
  STATIC_CHECK(toString(ErrorCode::ChunkTruncated) == "ChunkTruncated");
  STATIC_CHECK(toString(ErrorCode::UnsupportedClientVersion) == "UnsupportedClientVersion");
}
