#pragma once

/** @file
    The shared DBC-corpus sweep: for every table dbdgen generated for an era,
    read DBFilesClient/<Name>.dbc from a real client, decode it against the
    generated schema, write it back and require the bytes identical. Missing
    files (tables the era's DBD covers but the client does not ship) are
    counted, not failed; every present file must decode and round-trip. */

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/db/wdbc.hpp>
#include <wowlib/db/wdc/wdc.hpp>
#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/csv_listfile.hpp>
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
    // a freshly read, unmodified client table passes validation with zero
    // errors (every stored value fits its column, ids are unique)
    if (const auto valid = table.ensure_valid(); !valid)
      stats.failures.push_back(std::format("{}: {}", name, valid.error().message));
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
    // The one family whose identifier differs from its on-disk name: dbdgen
    // renames the hyphenated "Item-sparse" table to ItemSparseLegacy.
    const std::string base = name == "ItemSparseLegacy" ? "Item-sparse" : std::string{name};
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
    // a freshly read, unmodified client table passes validation with zero
    // errors (every stored value fits its column, ids are unique)
    if (const auto valid = table.ensure_valid(); !valid)
      stats.failures.push_back(std::format("{}: {}", name, valid.error().message));
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

  /** The tally of a structural (untyped) DB2 sweep. */
  struct ImageStats
  {
    int present = 0;                    /**< Files the client ships. */
    int parsed = 0;                     /**< Images the codec accepted. */
    int encrypted = 0;                  /**< Images carrying encrypted sections. */
    std::vector<std::string> failures;  /**< One line per parse failure. */
  };

  /** Every `.db2` path under `dbfilesclient/` in a listfile CSV (CsvListfile resolves
      single paths but does not enumerate).
      @param csv the listfile path.
      @return the lowercased db2 paths. */
  inline std::vector<std::string> db2_paths(const std::filesystem::path& csv)
  {
    std::vector<std::string> out;
    std::ifstream in{csv};
    std::string line;
    while (std::getline(in, line))
    {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
      const auto semi = line.find(';');
      if (semi == std::string::npos)
        continue;
      std::string path = line.substr(semi + 1);
      for (char& c : path)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (path.starts_with("dbfilesclient/") && path.ends_with(".db2"))
        out.push_back(path);
    }
    return out;
  }

  /** Structurally sweep a CASC client's WHOLE database corpus: parse every
      shipped .db2 as a raw WDC image (sections, field metadata, palettes) with
      NO generated schema involved. This is the breadth half of a CASC-era
      corpus proof — it covers every table the client ships without
      instantiating a thousand table templates in one TU (a Dragonflight
      all-tables TU peaks at 5.3 GB and OOM-kills hosted runners); the typed
      round-trip depth comes from the representative tables swept alongside.
      @param storage  the client's CASC storage.
      @param listfile the loaded community listfile.
      @param csv      the listfile path (enumerated directly).
      @param stats    the sweep tally. */
  inline void sweep_db2_images(fs::CascStorage& storage, const fs::CsvListfile& listfile,
                               const std::filesystem::path& csv, ImageStats& stats)
  {
    for (const std::string& path : db2_paths(csv))
    {
      const auto fdid = listfile.path_to_fdid(path);
      if (!fdid)
        continue;
      const auto data = storage.read_file(FileKey{*fdid});
      if (!data || data->size() < 4)
        continue;
      std::uint32_t magic = 0;
      std::memcpy(&magic, data->data(), 4);
      if (!db::wdc::is_wdc_magic(magic))
        continue;  // pre-Legion WDB2 images are swept typed instead
      ++stats.present;
      const auto img = db::wdc::WdcImage::parse(*data);
      if (!img)
      {
        if (stats.failures.size() < 20)
          stats.failures.push_back(path + ": " + img.error().message);
        continue;
      }
      ++stats.parsed;
      if (std::ranges::any_of(img->sections, [](const auto& s) { return s.encrypted; }))
        ++stats.encrypted;
    }
  }

  /** The image-sweep failure list joined for a single INFO() block. */
  inline std::string join_failures(const ImageStats& stats)
  {
    std::string out;
    for (const std::string& failure : stats.failures)
    {
      out += failure;
      out += '\n';
    }
    return out;
  }

  /** Sweep one table of a CASC-era client: resolve
      dbfilesclient/<name>.db2 through the community listfile, decode,
      re-encode, and compare — byte-perfect for the WDB2 era, semantic
      (re-decode yields the same record set by id, encrypted images preserved
      verbatim) for WDC*.
      @tparam Tbl the generated table type of the era.
      @param storage  the client's CASC storage.
      @param listfile the loaded community listfile.
      @param name     the WoWDBDefs table name.
      @param stats    the sweep tally.
      @param byte_perfect require memcmp equality (WDB2) instead of the
                          semantic compare (WDC*). */
  template <typename Tbl>
  void sweep_table_casc(fs::CascStorage& storage, const fs::CsvListfile& listfile,
                        std::string_view name, CorpusStats& stats, bool byte_perfect)
  {
    std::string base = name == "ItemSparseLegacy" ? "Item-sparse" : std::string{name};
    for (char& c : base)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // A CASC-era client is mixed until Legion retires .dbc: 6.2.3 ships 270
    // .db2 beside 247 .dbc. The fallback must key off the READ, not the
    // listfile lookup: the community listfile spans every expansion, so a
    // table that is .dbc here still resolves its later .db2 name to a
    // FileDataID this client does not carry (that alone hid 94 of 6.2.3's
    // 164 present tables).
    Result<FileBuffer> data = make_error(ErrorCode::FileNotFound, "no candidate");
    for (const char* ext : {"db2", "dbc"})
    {
      const auto fdid = listfile.path_to_fdid(std::format("dbfilesclient/{}.{}", base, ext));
      if (!fdid)
        continue;
      if (auto candidate = storage.read_file(FileKey{*fdid}))
      {
        data = std::move(candidate);
        break;
      }
    }
    if (!data)
    {
      ++stats.missing;  // not listed, or listed but not shipped by this client
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
    // a freshly read, unmodified client table passes validation with zero
    // errors (every stored value fits its column, ids are unique)
    if (const auto valid = table.ensure_valid(); !valid)
      stats.failures.push_back(std::format("{}: {}", name, valid.error().message));
    const auto written = table.write();
    if (!written)
    {
      stats.failures.push_back(
        std::format("{}: write failed: {}", name, written.error().message));
      return;
    }

    bool preserve = byte_perfect;
    if constexpr (requires { table.encrypted_sections(); })
      preserve = preserve || !table.encrypted_sections().empty();
    if (preserve)
    {
      // WDB2 round-trips byte-perfectly; an encrypted WDC image is preserved
      // verbatim by write() (the encrypted records share the file layout).
      if (written->size() != data->size()
          || std::memcmp(written->data(), data->data(), data->size()) != 0)
        stats.failures.push_back(
          std::format("{}: {}", name, describe_divergence(*data, *written)));
      return;
    }

    // WDC*: canonical re-encode — re-reading must yield the same record set.
    // A write coalesces duplicate rows, which can reorder multi-section
    // tables, so compare sorted by id; id-less records cannot coalesce and
    // compare in order.
    Tbl reread;
    if (const auto r = reread.read(*written); !r)
    {
      stats.failures.push_back(
        std::format("{}: reread failed: {}", name, r.error().message));
      return;
    }
    if constexpr (requires { table.records.front().id; })
    {
      const auto by_id = [](const auto& a, const auto& b) { return a.id < b.id; };
      std::ranges::sort(table.records, by_id);
      std::ranges::sort(reread.records, by_id);
    }
    if (reread.records != table.records)
      stats.failures.push_back(std::format(
        "{}: re-decode diverges ({} vs {} records)", name,
        reread.records.size(), table.records.size()));
  }
}
