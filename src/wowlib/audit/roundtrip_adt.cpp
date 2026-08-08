/** @file
    The ADT round-trip driver: the write -> parse -> write-again stability
    compare of one terrain tile (ADT is not byte-perfect — alpha maps are
    re-encoded and the MHDR/MCIN/MCNK offset tables re-derived), modeled on
    tests/integration/test_adt_roundtrip.cpp with the reflection diff replaced
    by the write-stability byte compare and returned outcomes in place of
    Catch2 assertions.

    The on-disk alpha-map bit depth is a per-MAP property (the WDT MPHD
    big-alpha flags) that wowlib does not resolve itself; every call re-derives
    it by reading the map's own WDT next to the tile ("<dir>/<map>.wdt" from
    the tile's parent directory). Nothing is cached — calls stay independent,
    and the sweep feeds tiles map by map anyway. */

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <wowlib/audit/detail.hpp>
#include <wowlib/core/client_builds.hpp>
#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

namespace
{
  using namespace wowlib;
  using namespace wowlib::audit;
  using namespace wowlib::formats;

  /** The on-disk alpha bit depth a map's WDT MPHD flags select: 4096-byte
      8-bit maps when adt_has_big_alpha (0x4) or adt_has_height_texturing
      (0x80) is set, else 2048-byte 4-bit.
      @param mphd_flags the map's MPHD flags.
      @return the alpha format to pass to ADT read()/write(). */
  adt::AlphaFormat alpha_format_of(std::uint32_t mphd_flags)
  {
    return (mphd_flags & 0x4) || (mphd_flags & 0x80) ? adt::AlphaFormat::highres_8bit
                                                     : adt::AlphaFormat::lowres_4bit;
  }

  /** The map WDT sitting next to a tile: "world\\maps\\azeroth\\azeroth_32_48.adt"
      -> "world\\maps\\azeroth\\azeroth.wdt" (the parent directory names the map).
      @param tile_path the canonical tile path.
      @return the canonical WDT path, or empty when the tile has no
              map-directory shape. */
  std::string map_wdt_of(std::string_view tile_path)
  {
    const auto last_sep = tile_path.rfind('\\');
    if (last_sep == std::string_view::npos)
      return {};
    const std::string_view dir = tile_path.substr(0, last_sep);
    const auto prev_sep = dir.rfind('\\');
    const std::string_view map =
      prev_sep == std::string_view::npos ? dir : dir.substr(prev_sep + 1);
    if (map.empty())
      return {};
    return std::format("{}\\{}.wdt", dir, map);
  }

  /** Read the tile's map WDT and derive the alpha format, or fail the report.
      @tparam V the client version.
      @param fs     the client filesystem.
      @param path   the canonical tile path.
      @param report the report accumulating the file's outcome.
      @param alpha  [out] the derived alpha format.
      @return nullopt on success, or the failed outcome. */
  template <ClientVersion V>
  std::optional<RoundtripReport> derive_alpha(fs::FileSystem& fs, const std::string& path,
                                              RoundtripReport& report,
                                              adt::AlphaFormat& alpha)
  {
    const std::string wdt_path = map_wdt_of(path);
    if (wdt_path.empty())
      return audit::detail::fail_with(report, "wdt locate",
                               "the tile path has no map directory to derive the WDT from");
    const auto raw = fs.read_file(FileKey{wdt_path});
    if (!raw)
    {
      // An unreadable map WDT is an addressing limitation, not a parse
      // failure: community-listfile placeholder maps ("unkmaps/...") derive a
      // synthetic WDT name no listfile entry or name hash resolves. Without
      // the WDT there is no alpha format, so the tile cannot be audited.
      return audit::detail::skipped("no-map-wdt");
    }
    wdt::root::WDTRoot<V> root;
    if (auto r = root.read(*raw); !r)
      return audit::detail::fail_with(report, "wdt parse",
                               std::format("{}: {}", wdt_path, r.error().message));
    alpha = alpha_format_of(root.header.flags);
    return std::nullopt;
  }

  /** Stability round-trip of one monolithic (pre-Cata) tile: read from the
      client, write to a buffer, parse that back, write again, byte-compare
      the two writes.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical tile path.
      @return the outcome. */
  template <ClientVersion V>
  RoundtripReport roundtrip_adt(fs::FileSystem& fs, const std::string& path)
  {
    RoundtripReport report;
    adt::AlphaFormat alpha{};
    if (auto fail = derive_alpha<V>(fs, path, report, alpha))
      return *fail;

    adt::ADT<V> tile;
    if (auto r = tile.read(fs, FileKey{path}, alpha); !r)
    {
      if (r.error().code == ErrorCode::EncryptedContent)
        return audit::detail::skipped("encrypted");
      return audit::detail::fail_with(report, "read", r.error().message);
    }

    const auto first = tile.write_file(adt::FileKind::monolithic, alpha);
    if (!first)
      return audit::detail::fail_with(report, "write", first.error().message);

    adt::ADT<V> reparsed;
    reparsed.alpha_format = alpha;
    if (auto r = reparsed.parse_file(*first, adt::FileKind::monolithic); !r)
      return audit::detail::fail_with(report, "reparse", r.error().message);

    const auto second = reparsed.write_file(adt::FileKind::monolithic, alpha);
    if (!second)
      return audit::detail::fail_with(report, "rewrite", second.error().message);
    if (auto divergence = audit::detail::first_divergence_chunked(*first, *second))
      return audit::detail::fail_with(report, "compare", *divergence);
    return report;
  }

  /** Stability round-trip of one Cata+ split tile: read (root + _tex0 + _obj0
      merged, _obj1/_lod preserved verbatim), write each physical file, parse
      them all back into a fresh entity, write each again and byte-compare the
      two writes per physical kind.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical root tile path.
      @return the outcome. */
  template <ClientVersion V>
  RoundtripReport roundtrip_adt_split(fs::FileSystem& fs, const std::string& path)
  {
    RoundtripReport report;
    adt::AlphaFormat alpha{};
    if (auto fail = derive_alpha<V>(fs, path, report, alpha))
      return *fail;

    adt::ADT<V> tile;
    if (auto r = tile.read(fs, FileKey{path}, alpha); !r)
    {
      if (r.error().code == ErrorCode::EncryptedContent)
        return audit::detail::skipped("encrypted");
      return audit::detail::fail_with(report, "read", r.error().message);
    }

    constexpr std::array kinds{adt::FileKind::root, adt::FileKind::tex0, adt::FileKind::obj0};
    constexpr std::array names{"root", "_tex0", "_obj0"};

    adt::ADT<V> reparsed;
    reparsed.alpha_format = alpha;
    reparsed.chunks.assign(256, adt::MapChunk<V>{});

    std::array<FileBuffer, kinds.size()> firsts{};
    for (std::size_t i = 0; i < kinds.size(); ++i)
    {
      auto first = tile.write_file(kinds[i], alpha);
      if (!first)
        return audit::detail::fail_with(report, std::format("write ({})", names[i]),
                                 first.error().message);
      if (auto r = reparsed.parse_file(*first, kinds[i]); !r)
        return audit::detail::fail_with(report, std::format("reparse ({})", names[i]),
                                 r.error().message);
      firsts[i] = std::move(*first);
    }
    // _obj1/_lod are round-tripped verbatim by write(); mirror that here so
    // the fresh entity is whole (they do not enter the per-kind compares).
    if constexpr (requires { tile.obj1_data; })
    {
      reparsed.obj1_data = tile.obj1_data;
      reparsed.lod_data = tile.lod_data;
    }

    for (std::size_t i = 0; i < kinds.size(); ++i)
    {
      const auto second = reparsed.write_file(kinds[i], alpha);
      if (!second)
        return audit::detail::fail_with(report, std::format("rewrite ({})", names[i]),
                                 second.error().message);
      if (auto divergence = audit::detail::first_divergence_chunked(firsts[i], *second))
        return audit::detail::fail_with(report, std::format("compare ({})", names[i]), *divergence);
    }
    return report;
  }
}

namespace wowlib::audit::detail
{
  RoundtripReport FormatDrivers::adt(fs::FileSystem& fs, const std::string& path,
                                     ClientVersion version)
  {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = with_version(version, [&]<ClientVersion V>() {
      if constexpr (version_supported(formats::adt::adt_versions, V))
      {
        if constexpr (V < builds::Cata)
          report = guarded([&] { return roundtrip_adt<V>(fs, path); });
        else
          report = guarded([&] { return roundtrip_adt_split<V>(fs, path); });
      }
    });
    if (!matched)
      return unrecognized_version(version);
    return report;
  }
}
