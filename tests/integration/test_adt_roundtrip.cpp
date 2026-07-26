#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/wdt/wdt.hpp>
#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::formats;

namespace
{
  /** The first bit-level divergence between two decoded values, as a member
      path, or nullopt when identical. Bitwise (memcmp) at the leaves so NaN
      floats in client heights — bit-preserved by the round-trip — do not read
      as divergence (the M2 diff_value pattern, generalized over the reflected
      member tree so it walks StringBlock, the liquid entities and the trait
      bases too). */
  template <typename T>
  std::optional<std::string> diff_value(const T& a, const T& b)
  {
    if constexpr (formats::detail::is_vector_v<T>)
    {
      using U = typename T::value_type;
      if (a.size() != b.size())
        return std::format(": size {} vs {}", a.size(), b.size());
      for (std::size_t i = 0; i < a.size(); ++i)
        if (auto d = diff_value(a[i], b[i]))
          return std::format("[{}]{}", i, *d);
      return std::nullopt;
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
      if (a != b)
        return std::format(": \"{}\" vs \"{}\"", a, b);
      return std::nullopt;
    }
    else if constexpr (std::is_trivially_copyable_v<T>)
    {
      if (std::memcmp(&a, &b, sizeof(T)) != 0)
        return std::optional<std::string>{": bytes differ"};
      return std::nullopt;
    }
    else
    {
      static constexpr auto members = formats::detail::members_of<T>();
      std::optional<std::string> out;
      template for (constexpr auto m : members)
      {
        if (!out)
          if (auto d = diff_value(a.[:m:], b.[:m:]))
            out = std::format(".{}{}", std::meta::identifier_of(m), *d);
      }
      return out;
    }
  }

  /** Semantic round-trip of one tile: read it from the client, canonically
      rewrite to a buffer, parse the buffer back with the same alpha format, and
      require decoded equality (ADT is not byte-perfect — see adt-architecture). */
  template <ClientVersion V>
  void roundtrip_adt(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    INFO(label);
    adt::ADT<V> a;
    {
      const auto r = a.read(fs, key);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    CHECK(a.mver == adt::adt_version_18);
    REQUIRE(a.cells.size() == 256);

    const auto buf = a.write_monolithic();
    REQUIRE(buf.has_value());

    adt::ADT<V> b;
    b.alpha_format = a.alpha_format;
    {
      const auto r = b.parse_file(*buf, adt::FileKind::monolithic);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }

    const auto d = diff_value(a, b);
    INFO(d.value_or(""));
    CHECK_FALSE(d.has_value());
  }
}

TEST_CASE("3.3.5a ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::mpq_client_name,
                                  .version = versions::wotlk});
  REQUIRE(fs.has_value());

  const std::vector<std::string> maps{
    "Azeroth", "Kalimdor", "Northrend", "Expansion01", "PVPZone01", "Ulduar",
  };

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdt_path = std::format("World/Maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdt_path))
    {
      WARN("not in client, skipped: " + wdt_path);
      continue;
    }
    wdt::root::WDTRoot<versions::wotlk> root;
    REQUIRE(root.read(*fs->read_file(FileKey{wdt_path})).has_value());

    int tiles_this_map = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tiles_this_map < 4; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("World/Maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtrip_adt<versions::wotlk>(*fs, FileKey{adt}, adt);
      ++tiles_this_map;
      ++verified;
    }
  }
  CHECK(verified >= 6);
}
