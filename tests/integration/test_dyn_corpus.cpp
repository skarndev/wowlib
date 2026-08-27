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
  bool sameCells(const db::DynTable& a, const db::DynTable& b,
                  std::string& why)
  {
    if (a.rowCount() != b.rowCount())
    {
      why = std::format("row counts {} vs {}", a.rowCount(), b.rowCount());
      return false;
    }
    const auto schema = a.schema();
    std::size_t idCol = SIZE_MAX;
    for (std::size_t c = 0; c < schema.size(); ++c)
      if (schema[c].isId)
      {
        idCol = c;
        break;
      }
    // Key by id only when the ids are actually unique — tables can carry an
    // unpopulated (all-zero) $id$ column, and those compare positionally
    // (which is also what the typed engine's own round-trip preserves).
    std::unordered_map<std::uint32_t, std::size_t> bById;
    if (idCol != SIZE_MAX)
      for (std::size_t r = 0; r < b.rowCount(); ++r)
        if (const auto [it, fresh] = bById.emplace(
                static_cast<std::uint32_t>(b.getInt(r, idCol).value()), r);
            !fresh)
        {
          idCol = SIZE_MAX;
          break;
        }
    for (std::size_t r = 0; r < a.rowCount(); ++r)
    {
      std::size_t rb = r;
      if (idCol != SIZE_MAX)
      {
        const auto found = bById.find(
            static_cast<std::uint32_t>(a.getInt(r, idCol).value()));
        if (found == bById.end())
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
              return a.getInt(r, c, e).value() != b.getInt(rb, c, e).value();
            case db::ColumnType::Float:
              return a.getFloat(r, c, e).value() != b.getFloat(rb, c, e).value();
            case db::ColumnType::String:
            case db::ColumnType::LocString:
              return a.getString(r, c, e).value() != b.getString(rb, c, e).value();
          }
          return false;
        };
        const std::size_t slots =
            col.type == db::ColumnType::LocString ? col.localeCount
                                                  : col.arrayLen;
        for (std::size_t e = 0; e < slots; ++e)
          if (differs(e))
          {
            why = std::format("row {} column {} element {}", r,
                              col.nameView(), e);
            return false;
          }
        if (col.type == db::ColumnType::LocString &&
            a.locstringFlags(r, c).value() != b.locstringFlags(rb, c).value())
        {
          why = std::format("row {} column {} flags", r, col.nameView());
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
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mpqClient()),
                                  .version = versions::Wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());

  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();
  tests::CorpusStats stats;
  for (std::size_t i = 0; i < catalog.tableCount(); ++i)
  {
    const auto schema = catalog.lookup(catalog.tableName(i), versions::Wotlk);
    if (!schema)
      continue; // no wotlk-era definition for this table
    const auto data = opened->readFile(
        FileKey{std::format("DBFilesClient/{}.dbc", schema->diskName)});
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
    db::DynTable table = db::DynTable::fromSchema(*schema, versions::Wotlk);
    if (const auto r = table.read(*data); !r)
    {
      stats.failures.push_back(std::format("{}: read failed: {}", schema->name,
                                           r.error().message));
      continue;
    }
    if (const auto valid = table.ensureValid(); !valid)
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
          tests::describeDivergence(*data, *written)));
  }

  INFO(tests::joinFailures(stats));
  CHECK(stats.failures.empty());
  CHECK(stats.present >= 150); // same floor as the typed sweep
}

TEST_CASE("3.3.5a: Map.dbc truth spot-checks through generic accessors",
          "[integration][db][dyn]")
{
  auto opened = MpqStorage::open({.dataDir = tests::dataDir(tests::mpqClient()),
                                  .version = versions::Wotlk,
                                  .locale = Locale::enUS});
  REQUIRE(opened.has_value());
  const auto data = opened->readFile(FileKey{"DBFilesClient/Map.dbc"});
  REQUIRE(data.has_value());

  auto table = db::DynTable::open("Map", versions::Wotlk);
  REQUIRE(table.has_value());
  REQUIRE(table->read(*data).has_value());

  const auto directory = table->columnIndex("directory").value();
  const auto mapName = table->columnIndex("map_name").value();
  const auto row = [&](std::uint32_t id) { return table->findById(id).value(); };

  CHECK(table->getString(row(0), directory).value() == "Azeroth");
  CHECK(table->getString(row(1), directory).value() == "Kalimdor");
  CHECK(table->getString(row(530), directory).value() == "Expansion01");
  CHECK(table->getString(row(571), directory).value() == "Northrend");
  // enUS is locale slot 0 in the LocString layout.
  CHECK_FALSE(table->getString(row(0), mapName, 0).value().empty());
}

TEST_CASE("9.2.7: the DB2 corpus decodes and round-trips through DynTable",
          "[integration][db][dyn]")
{
  const auto listfileCsv = tests::requireListfile();
  auto listfile = CsvListfile::load(listfileCsv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::cascClient(),
                                    .build = 45745});
  REQUIRE(storage.has_value());

  // The catalog is keyed by identifier name; db2 paths carry the DISK name.
  const db::SchemaCatalog& catalog = db::SchemaCatalog::embedded();
  std::unordered_map<std::string, std::string> diskToTable;
  for (std::size_t i = 0; i < catalog.tableCount(); ++i)
    if (const auto schema =
            catalog.lookup(catalog.tableName(i), versions::Shadowlands))
      diskToTable.emplace(lower(schema->diskName),
                            std::string{schema->name});

  int present = 0, noSchema = 0, roundTripped = 0, withEncrypted = 0;
  std::vector<std::string> failures;
  std::ifstream in{listfileCsv};
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
    const auto tableIt = diskToTable.find(stem);
    if (tableIt == diskToTable.end())
    {
      ++noSchema;
      continue;
    }
    const auto fdid = listfile->pathToFdid(path);
    if (!fdid)
      continue;
    const auto data = storage->readFile(FileKey{*fdid});
    if (!data || data->size() < 4)
      continue;
    ++present;

    const auto schema =
        catalog.lookup(tableIt->second, versions::Shadowlands);
    REQUIRE(schema.has_value());
    db::DynTable table = db::DynTable::fromSchema(*schema, versions::Shadowlands);
    if (const auto r = table.read(*data); !r)
    {
      if (failures.size() < 20)
        failures.push_back(std::format("{}: read failed: {}", tableIt->second,
                                       r.error().message));
      continue;
    }
    if (!table.fullyDecoded())
      ++withEncrypted;
    const auto written = table.write();
    if (!written)
    {
      if (failures.size() < 20)
        failures.push_back(std::format("{}: write failed: {}", tableIt->second,
                                       written.error().message));
      continue;
    }
    // The WDC guarantee is SEMANTIC: write → re-read → identical cells (the
    // writer re-derives copy tables and re-encodes pallets by design, so the
    // bytes legitimately differ — same bar as the typed engine documents).
    db::DynTable back = db::DynTable::fromSchema(*schema, versions::Shadowlands);
    if (const auto r = back.read(*written); !r)
    {
      if (failures.size() < 20)
        failures.push_back(std::format("{}: re-read failed: {}",
                                       tableIt->second, r.error().message));
      continue;
    }
    std::string why;
    if (sameCells(table, back, why))
      ++roundTripped;
    else if (failures.size() < 20)
      failures.push_back(std::format("{}: semantic divergence at {}",
                                     tableIt->second, why));
  }

  INFO("failures:\n" << [&] {
    std::string s;
    for (const auto& f : failures)
      s += f + '\n';
    return s;
  }());
  CHECK(failures.empty());
  INFO("listfile db2 paths without a catalog schema: " << noSchema);
  CHECK(present >= 700);          // ~830 tables have schemas + files locally
  CHECK(roundTripped == present);
  CHECK(withEncrypted >= 100);   // encrypted sections preserved, not decoded
}

TEST_CASE("9.2.7: ManifestInterfaceData truth through generic accessors",
          "[integration][db][dyn]")
{
  const auto listfileCsv = tests::requireListfile();
  auto listfile = CsvListfile::load(listfileCsv);
  REQUIRE(listfile.has_value());
  auto storage = CascStorage::open({.clientRoot = tests::cascClient(),
                                    .build = 45745});
  REQUIRE(storage.has_value());
  const auto fdid = listfile->pathToFdid("dbfilesclient/manifestinterfacedata.db2");
  REQUIRE(fdid.has_value());
  const auto data = storage->readFile(FileKey{*fdid});
  REQUIRE(data.has_value());

  auto table = db::DynTable::open("ManifestInterfaceData", versions::Shadowlands);
  REQUIRE(table.has_value());
  REQUIRE(table->read(*data).has_value());
  CHECK(table->fullyDecoded());
  CHECK(table->rowCount() > 50'000);

  const auto filePath = table->columnIndex("file_path").value();
  const auto fileName = table->columnIndex("file_name").value();
  const auto logo = table->findById(21).value();
  CHECK(table->getString(logo, filePath).value() == "Interface\\Cinematics\\");
  CHECK(table->getString(logo, fileName).value() == "Logo_1024.avi");
}
