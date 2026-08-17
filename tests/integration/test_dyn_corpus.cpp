#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include <wowlib/db/dyn_table.hpp>
#include <wowlib/db/schema_catalog.hpp>
#include <wowlib/db/wdc/wdc.hpp>
#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/csv_listfile.hpp>
#include <wowlib/fs/mpq/mpq_storage.hpp>

#include "dbc_corpus.hpp"
#include "integration_env.hpp"

/** @file
    The GENERIC-engine corpus parity sweeps: everything the typed corpus tests
    prove, re-proven through DynTable + the embedded SchemaCatalog — no
    generated table type anywhere. This is the acceptance bar for replacing
    the per-era classes on the binding surface: the same real client files,
    decoded by runtime schema, written back byte-identically. */

using namespace wowlib;
using namespace wowlib::fs;

namespace
{
  std::string lower(std::string_view text)
  {
    std::string out{text};
    for (char& c : out)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
  }

  /** Whether two tables hold identical data — the SEMANTIC round-trip
      equality the WDC formats guarantee (their writes re-derive copy tables
      and re-encode pallets, so byte identity is out of scope by design; the
      typed engine documents the same bar). Keyed tables compare row sets BY
      ID — a re-derived copy table may rematerialize cloned rows in a
      different order; keyless tables compare positionally. */
  bool same_cells(const db::DynTable& a, const db::DynTable& b,
                  std::string& why)
  {
    if (a.row_count() != b.row_count())
    {
      why = std::format("row counts {} vs {}", a.row_count(), b.row_count());
      return false;
    }
    const auto schema = a.schema();
    std::size_t id_col = SIZE_MAX;
    for (std::size_t c = 0; c < schema.size(); ++c)
      if (schema[c].is_id)
      {
        id_col = c;
        break;
      }
    // Key by id only when the ids are actually unique — tables can carry an
    // unpopulated (all-zero) $id$ column, and those compare positionally
    // (which is also what the typed engine's own round-trip preserves).
    std::unordered_map<std::uint32_t, std::size_t> b_by_id;
    if (id_col != SIZE_MAX)
      for (std::size_t r = 0; r < b.row_count(); ++r)
        if (const auto [it, fresh] = b_by_id.emplace(
                static_cast<std::uint32_t>(b.get_int(r, id_col).value()), r);
            !fresh)
        {
          id_col = SIZE_MAX;
          break;
        }
    for (std::size_t r = 0; r < a.row_count(); ++r)
    {
      std::size_t rb = r;
      if (id_col != SIZE_MAX)
      {
        const auto found = b_by_id.find(
            static_cast<std::uint32_t>(a.get_int(r, id_col).value()));
        if (found == b_by_id.end())
        {
          why = std::format("row {}: id missing after re-read", r);
          return false;
        }
        rb = found->second;
      }
      for (std::size_t c = 0; c < schema.size(); ++c)
      {
        const db::Column& col = schema[c];
        const auto differs = [&](std::size_t e) {
          switch (col.type)
          {
            case db::ColumnType::Int:
              return a.get_int(r, c, e).value() != b.get_int(rb, c, e).value();
            case db::ColumnType::Float:
              return a.get_float(r, c, e).value() != b.get_float(rb, c, e).value();
            case db::ColumnType::String:
            case db::ColumnType::LocString:
              return a.get_string(r, c, e).value() != b.get_string(rb, c, e).value();
          }
          return false;
        };
        const std::size_t slots =
            col.type == db::ColumnType::LocString ? col.locale_count
                                                  : col.array_len;
        for (std::size_t e = 0; e < slots; ++e)
          if (differs(e))
          {
            why = std::format("row {} column {} element {}", r,
                              col.name_view(), e);
            return false;
          }
        if (col.type == db::ColumnType::LocString &&
            a.locstring_flags(r, c).value() != b.locstring_flags(rb, c).value())
        {
          why = std::format("row {} column {} flags", r, col.name_view());
          return false;
        }
      }
    }
    return true;
  }
}

TEST_CASE("3.3.5a: the full DBC corpus round-trips through DynTable",
          "[integration][db][dyn]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::mpq_client()),
                                  .version = versions::wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());

  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();
  tests::CorpusStats stats;
  for (std::size_t i = 0; i < catalog.table_count(); ++i)
  {
    const auto schema = catalog.lookup(catalog.table_name(i), versions::wotlk);
    if (!schema)
      continue; // no wotlk-era definition for this table
    const auto data = opened->read_file(
        FileKey{std::format("DBFilesClient/{}.dbc", schema->disk_name)});
    if (!data)
    {
      ++stats.missing;
      continue;
    }
    ++stats.present;
    if (data->empty())
    {
      ++stats.empty;
      continue;
    }
    db::DynTable table = db::DynTable::from_schema(*schema, versions::wotlk);
    if (const auto r = table.read(*data); !r)
    {
      stats.failures.push_back(std::format("{}: read failed: {}", schema->name,
                                           r.error().message));
      continue;
    }
    if (const auto valid = table.ensure_valid(); !valid)
      stats.failures.push_back(
          std::format("{}: {}", schema->name, valid.error().message));
    const auto written = table.write();
    if (!written)
    {
      stats.failures.push_back(std::format("{}: write failed: {}", schema->name,
                                           written.error().message));
      continue;
    }
    if (written->size() != data->size() ||
        std::memcmp(written->data(), data->data(), data->size()) != 0)
      stats.failures.push_back(std::format(
          "{}: not byte-identical — {}", schema->name,
          tests::describe_divergence(*data, *written)));
  }

  INFO(tests::join_failures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 150); // same floor as the typed sweep
}

TEST_CASE("3.3.5a: Map.dbc truth spot-checks through generic accessors",
          "[integration][db][dyn]")
{
  auto opened = MpqStorage::open({.data_dir = tests::data_dir(tests::mpq_client()),
                                  .version = versions::wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());
  const auto data = opened->read_file(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());

  auto table = db::DynTable::open("Map", versions::wotlk);
  REQUIRE(table.has_value());
  REQUIRE(table->read(*data).has_value());

  const auto directory = table->column_index("directory").value();
  const auto map_name = table->column_index("map_name").value();
  const auto row = [&](std::uint32_t id) { return table->find_by_id(id).value(); };

  CHECK(table->get_string(row(0), directory).value() == "Azeroth");
  CHECK(table->get_string(row(1), directory).value() == "Kalimdor");
  CHECK(table->get_string(row(530), directory).value() == "Expansion01");
  CHECK(table->get_string(row(571), directory).value() == "Northrend");
  // enUS is locale slot 0 in the LocString layout.
  CHECK_FALSE(table->get_string(row(0), map_name, 0).value().empty());
}

TEST_CASE("9.2.7: the DB2 corpus decodes and round-trips through DynTable",
          "[integration][db][dyn]")
{
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = tests::casc_client(),
                                    .build = 45745});
  REQUIRE(storage.has_value());

  // The catalog is keyed by identifier name; db2 paths carry the DISK name.
  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();
  std::unordered_map<std::string, std::string> disk_to_table;
  for (std::size_t i = 0; i < catalog.table_count(); ++i)
    if (const auto schema =
            catalog.lookup(catalog.table_name(i), versions::shadowlands))
      disk_to_table.emplace(lower(schema->disk_name),
                            std::string{schema->name});

  int present = 0, no_schema = 0, round_tripped = 0, with_encrypted = 0;
  std::vector<std::string> failures;
  std::ifstream in{listfile_csv};
  std::string line;
  while (std::getline(in, line))
  {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
      line.pop_back();
    const auto semi = line.find(';');
    if (semi == std::string::npos)
      continue;
    const std::string path = lower(line.substr(semi + 1));
    if (!path.starts_with("dbfilesclient/") || !path.ends_with(".db2"))
      continue;
    const std::string stem = path.substr(14, path.size() - 14 - 4);
    const auto table_it = disk_to_table.find(stem);
    if (table_it == disk_to_table.end())
    {
      ++no_schema;
      continue;
    }
    const auto fdid = listfile->path_to_fdid(path);
    if (!fdid)
      continue;
    const auto data = storage->read_file(FileKey{*fdid});
    if (!data || data->size() < 4)
      continue;
    ++present;

    const auto schema =
        catalog.lookup(table_it->second, versions::shadowlands);
    REQUIRE(schema.has_value());
    db::DynTable table = db::DynTable::from_schema(*schema, versions::shadowlands);
    if (const auto r = table.read(*data); !r)
    {
      if (failures.size() < 20)
        failures.push_back(std::format("{}: read failed: {}", table_it->second,
                                       r.error().message));
      continue;
    }
    if (!table.fully_decoded())
      ++with_encrypted;
    const auto written = table.write();
    if (!written)
    {
      if (failures.size() < 20)
        failures.push_back(std::format("{}: write failed: {}", table_it->second,
                                       written.error().message));
      continue;
    }
    // The WDC guarantee is SEMANTIC: write → re-read → identical cells (the
    // writer re-derives copy tables and re-encodes pallets by design, so the
    // bytes legitimately differ — same bar as the typed engine documents).
    db::DynTable back = db::DynTable::from_schema(*schema, versions::shadowlands);
    if (const auto r = back.read(*written); !r)
    {
      if (failures.size() < 20)
        failures.push_back(std::format("{}: re-read failed: {}",
                                       table_it->second, r.error().message));
      continue;
    }
    std::string why;
    if (same_cells(table, back, why))
      ++round_tripped;
    else if (failures.size() < 20)
      failures.push_back(std::format("{}: semantic divergence at {}",
                                     table_it->second, why));
  }

  INFO("failures:\n" << [&] {
    std::string s;
    for (const auto& f : failures)
      s += f + '\n';
    return s;
  }());
  CHECK(failures.empty());
  INFO("listfile db2 paths without a catalog schema: " << no_schema);
  CHECK(present >= 700);          // ~830 tables have schemas + files locally
  CHECK(round_tripped == present);
  CHECK(with_encrypted >= 100);   // encrypted sections preserved, not decoded
}

TEST_CASE("9.2.7: ManifestInterfaceData truth through generic accessors",
          "[integration][db][dyn]")
{
  const auto listfile_csv = tests::require_listfile();
  auto listfile = CsvListfile::load(listfile_csv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.client_root = tests::casc_client(),
                                    .build = 45745});
  REQUIRE(storage.has_value());
  const auto fdid = listfile->path_to_fdid("dbfilesclient/manifestinterfacedata.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->read_file(FileKey{*fdid});
  REQUIRE(data.has_value());

  auto table = db::DynTable::open("ManifestInterfaceData", versions::shadowlands);
  REQUIRE(table.has_value());
  REQUIRE(table->read(*data).has_value());
  CHECK(table->fully_decoded());
  CHECK(table->row_count() > 50'000);

  const auto file_path = table->column_index("file_path").value();
  const auto file_name = table->column_index("file_name").value();
  const auto logo = table->find_by_id(21).value();
  CHECK(table->get_string(logo, file_path).value() == "Interface\\Cinematics\\");
  CHECK(table->get_string(logo, file_name).value() == "Logo_1024.avi");
}
