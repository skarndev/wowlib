#pragma once

/** @file
    The shared DBC-corpus sweep: for every table dbdgen generated for an era,
    read DBFilesClient/<Name>.dbc from a real client, decode it against the
    generated schema, write it back and require the bytes identical. Missing
    files (tables the era's DBD covers but the client does not ship) are
    counted, not failed; every present file must decode and round-trip. */

#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/db/wdbc.hpp>
#include <wowlib/fs/mpq/mpq_storage.hpp>

namespace wowlib::tests
{
  struct CorpusStats
  {
    int present = 0;                    /**< Tables the client ships. */
    int missing = 0;                    /**< Generated tables absent from the client. */
    int empty = 0;                      /**< Zero-byte files (stripped tables ship empty). */
    std::vector<std::string> failures;  /**< One line per decode/round-trip failure. */
  };

  /** Locate the first divergent byte and describe which region it lands in. */
  inline std::string describe_divergence(std::span<const std::byte> original,
                                         std::span<const std::byte> written)
  {
    const std::size_t common = std::min(original.size(), written.size());
    std::size_t at = 0;
    while (at < common && original[at] == written[at])
      ++at;
    db::WdbcHeader header{};
    std::memcpy(&header, original.data(), std::min(sizeof header, original.size()));
    std::string region = "header";
    if (at >= sizeof header + std::size_t{header.record_count} * header.record_size)
      region = "string block";
    else if (at >= sizeof header)
      region = std::format("record {} byte {}", (at - sizeof header) / header.record_size,
                           (at - sizeof header) % header.record_size);
    return std::format("first divergence at {:#x} ({}), sizes {} vs {}", at, region,
                       original.size(), written.size());
  }

  /** Sweep one table: read, decode, re-encode, memcmp. */
  template <typename Tbl>
  void sweep_table(fs::MpqStorage& storage, std::string_view name, CorpusStats& stats)
  {
    const auto data = storage.read_file(FileKey{std::format("DBFilesClient/{}.dbc", name)});
    if (!data)
    {
      ++stats.missing;
      return;
    }
    ++stats.present;
    if (data->empty())
    {
      // A handful of vanilla-era tables ship as zero-byte files (e.g.
      // SpellAuraNames); there is nothing to decode or round-trip.
      ++stats.empty;
      return;
    }

    Tbl table;
    if (const auto r = table.read(*data); !r)
    {
      stats.failures.push_back(std::format("{}: read failed: {}", name, r.error().message));
      return;
    }
    const auto written = table.write();
    if (!written)
    {
      stats.failures.push_back(
        std::format("{}: write failed: {}", name, written.error().message));
      return;
    }
    if (written->size() != data->size()
        || std::memcmp(written->data(), data->data(), data->size()) != 0)
      stats.failures.push_back(
        std::format("{}: {}", name, describe_divergence(*data, *written)));
  }

  /** Sweep one table of a mixed .dbc/.db2 era (Cata..WoD): try
      DBFilesClient/<Name>.db2 first, fall back to <Name>.dbc; decode,
      re-encode, memcmp — both formats are byte-perfect round-trips.
      @tparam Tbl the generated table type of the era.
      @param storage the client's MPQ chain.
      @param name    the WoWDBDefs table name.
      @param stats   the sweep tally. */
  template <typename Tbl>
  void sweep_table_mixed(fs::MpqStorage& storage, std::string_view name, CorpusStats& stats)
  {
    // The one filename that differs from its table name in the WDB2 era.
    const std::string base = name == "ItemSparse" ? "Item-sparse" : std::string{name};
    auto data = storage.read_file(FileKey{std::format("DBFilesClient/{}.db2", base)});
    if (!data)
      data = storage.read_file(FileKey{std::format("DBFilesClient/{}.dbc", base)});
    if (!data)
    {
      ++stats.missing;
      return;
    }
    ++stats.present;
    if (data->empty())
    {
      ++stats.empty;
      return;
    }

    Tbl table;
    if (const auto r = table.read(*data); !r)
    {
      stats.failures.push_back(std::format("{}: read failed: {}", name, r.error().message));
      return;
    }
    const auto written = table.write();
    if (!written)
    {
      stats.failures.push_back(
        std::format("{}: write failed: {}", name, written.error().message));
      return;
    }
    if (written->size() != data->size()
        || std::memcmp(written->data(), data->data(), data->size()) != 0)
      stats.failures.push_back(
        std::format("{}: {}", name, describe_divergence(*data, *written)));
  }

  /** The failure list joined for a single INFO() block. */
  inline std::string join_failures(const CorpusStats& stats)
  {
    std::string out;
    for (const std::string& failure : stats.failures)
    {
      out += failure;
      out += '\n';
    }
    return out;
  }
}
