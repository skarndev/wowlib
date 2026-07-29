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

  /** The on-disk alpha bit depth a map's WDT MPHD flags select: 4096-byte 8-bit
      maps when adt_has_big_alpha (0x4) or adt_has_height_texturing (0x80) is set,
      else 2048-byte 4-bit. wowlib does not resolve this itself — the caller reads
      the WDT (as this test does) and passes the format to ADT read()/write(). */
  adt::AlphaFormat alpha_format_of(std::uint32_t mphd_flags)
  {
    return (mphd_flags & 0x4) || (mphd_flags & 0x80) ? adt::AlphaFormat::highres_8bit
                                                     : adt::AlphaFormat::lowres_4bit;
  }

  /** Structural invariants every decoded chunk must satisfy — a guard against a
      SILENT misparse (a stream misalignment that a semantic round-trip cannot
      catch, since both sides misparse identically). A chunk either has a full
      terrain grid or none; alpha/shadow maps are the full 64x64 edit surface;
      the alpha-map list is aligned with the layers. */
  template <typename Chunk>
  void check_chunk(const Chunk& c, std::size_t index, const std::string& label)
  {
    INFO(label << " chunk " << index);
    CHECK((c.heights.empty() || c.heights.size() == 145));
    CHECK((c.normals.empty() || c.normals.size() == 145));
    CHECK((c.shadow_map.empty() || c.shadow_map.size() == 4096));
    CHECK(c.alpha_maps.size() == c.layers.size());
    for (const auto& map : c.alpha_maps)
      CHECK((map.empty() || map.size() == 4096));
    if constexpr (requires { c.vertex_colors; })
      CHECK((c.vertex_colors.empty() || c.vertex_colors.size() == 145));
  }

  /** Semantic round-trip of one monolithic (pre-Cata) tile: read it from the
      client, canonically rewrite to a buffer, parse the buffer back with the
      same alpha format, and require decoded equality (ADT is not byte-perfect —
      see adt-architecture). */
  template <ClientVersion V>
  void roundtrip_adt(fs::FileSystem& fs, const FileKey& key, adt::AlphaFormat af,
                     const std::string& label)
  {
    INFO(label);
    adt::ADT<V> a;
    {
      const auto r = a.read(fs, key, af);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    CHECK(a.mver == adt::adt_version_18);
    REQUIRE(a.chunks.size() == 256);
    for (std::size_t i = 0; i < a.chunks.size(); ++i)
      check_chunk(a.chunks[i], i, label);

    const auto buf = a.write_file(adt::FileKind::monolithic, af);
    REQUIRE(buf.has_value());

    adt::ADT<V> b;
    b.alpha_format = af;
    {
      const auto r = b.parse_file(*buf, adt::FileKind::monolithic);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }

    const auto d = diff_value(a, b);
    INFO(d.value_or(""));
    CHECK_FALSE(d.has_value());
  }

  /** Semantic round-trip of one Cata+ split tile: read (root + _tex0 + _obj0
      merged, _obj1/_lod preserved verbatim), rewrite each physical file to a
      buffer, parse them all back into a fresh entity, and require decoded
      equality. */
  template <ClientVersion V>
  void roundtrip_adt_split(fs::FileSystem& fs, const FileKey& key, adt::AlphaFormat af,
                           const std::string& label)
  {
    INFO(label);
    adt::ADT<V> a;
    {
      const auto r = a.read(fs, key, af);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    REQUIRE(a.chunks.size() == 256);
    for (std::size_t i = 0; i < a.chunks.size(); ++i)
      check_chunk(a.chunks[i], i, label);

    adt::ADT<V> b;
    b.alpha_format = af;
    b.chunks.assign(256, adt::MapChunk<V>{});
    for (const auto kind : {adt::FileKind::root, adt::FileKind::tex0, adt::FileKind::obj0})
    {
      const auto buf = a.write_file(kind, af);
      REQUIRE(buf.has_value());
      const auto r = b.parse_file(*buf, kind);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    // _obj1/_lod are round-tripped verbatim by write(); mirror that here
    b.obj1_data = a.obj1_data;
    b.lod_data = a.lod_data;

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
    const auto af = alpha_format_of(root.header.flags);

    int tiles_this_map = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tiles_this_map < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("World/Maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtrip_adt<versions::wotlk>(*fs, FileKey{adt}, af, adt);
      ++tiles_this_map;
      ++verified;
    }
  }
  CHECK(verified >= 6);
}

TEST_CASE("1.12.2 ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::vanilla_client_name,
                                  .version = versions::vanilla,
                                  .locale = tests::vanilla_locale});
  REQUIRE(fs.has_value());

  const std::vector<std::string> maps{"Azeroth", "Kalimdor"};

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdt_path = std::format("World/Maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdt_path))
    {
      WARN("not in client, skipped: " + wdt_path);
      continue;
    }
    wdt::root::WDTRoot<versions::vanilla> root;
    REQUIRE(root.read(*fs->read_file(FileKey{wdt_path})).has_value());
    const auto af = alpha_format_of(root.header.flags);

    int tiles_this_map = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tiles_this_map < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("World/Maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtrip_adt<versions::vanilla>(*fs, FileKey{adt}, af, adt);
      ++tiles_this_map;
      ++verified;
    }
  }
  CHECK(verified >= 2);
}

TEST_CASE("2.4.3 ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::tbc_client_name,
                                  .version = versions::tbc,
                                  .locale = tests::tbc_locale});
  REQUIRE(fs.has_value());

  // the two vanilla continents plus Outland (Expansion01), TBC's new continent
  const std::vector<std::string> maps{"Azeroth", "Kalimdor", "Expansion01"};

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdt_path = std::format("World/Maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdt_path))
    {
      WARN("not in client, skipped: " + wdt_path);
      continue;
    }
    wdt::root::WDTRoot<versions::tbc> root;
    REQUIRE(root.read(*fs->read_file(FileKey{wdt_path})).has_value());
    const auto af = alpha_format_of(root.header.flags);

    int tiles_this_map = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tiles_this_map < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("World/Maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtrip_adt<versions::tbc>(*fs, FileKey{adt}, af, adt);
      ++tiles_this_map;
      ++verified;
    }
  }
  CHECK(verified >= 3);
}

TEST_CASE("9.2.7 split ADTs re-read equal after a canonical rewrite",
          "[integration][formats][adt]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile = tests::require_listfile();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::casc_client_name,
                                  .version = versions::shadowlands,
                                  .listfile_csv = listfile});
  REQUIRE(fs.has_value());

  const std::vector<std::string> maps{"kultiras", "azeroth", "kalimdor"};

  int verified = 0;
  for (const auto& map : maps)
  {
    const std::string wdt_path = std::format("world/maps/{0}/{0}.wdt", map);
    if (!fs->exists(wdt_path))
    {
      WARN("not in client, skipped: " + wdt_path);
      continue;
    }
    wdt::root::WDTRoot<versions::shadowlands> root;
    const auto raw = fs->read_file(FileKey{wdt_path});
    REQUIRE(raw.has_value());
    REQUIRE(root.read(*raw).has_value());
    const auto af = alpha_format_of(root.header.flags);

    int tiles_this_map = 0;
    for (std::size_t i = 0; i < root.tiles.size() && tiles_this_map < 30; ++i)
    {
      if (!(root.tiles[i].flags & 0x1))
        continue;
      const std::size_t x = i % 64, y = i / 64;
      const std::string adt = std::format("world/maps/{0}/{0}_{1}_{2}.adt", map, x, y);
      if (!fs->exists(adt))
        continue;
      roundtrip_adt_split<versions::shadowlands>(*fs, FileKey{adt}, af, adt);
      ++tiles_this_map;
      ++verified;
    }
  }
  CHECK(verified >= 3);
}
