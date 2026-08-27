#include <wowlib/db/schema_catalog.hpp>

#include <fstream>
#include <utility>

#include <wowlib/db/schema_blob.hpp>

namespace wowlib::db {
  SchemaCatalog SchemaCatalog::_materialize(std::span<const unsigned char> bytes, std::vector<unsigned char> owned) {
    const blob::View view{bytes};
    SchemaCatalog out{};
    out._owned = std::move(owned);

    out._eras.reserve(view.eraCount());
    for (std::size_t i = 0; i < view.eraCount(); ++i) out._eras.push_back(view.era(i));

    out._columns.reserve(view.columnCount());
    for (std::size_t i = 0; i < view.columnCount(); ++i) {
      const blob::ColumnEntry entry = view.column(i);
      Column col{};
      col.name = view.stringAt(entry.nameOff).data();
      col.type = entry.type;
      col.bits = entry.bits;
      col.isSigned = entry.isSigned;
      col.arrayLen = entry.arrayLen;
      col.localeCount = entry.localeCount;
      col.isId = entry.isId;
      col.isRelation = entry.isRelation;
      col.noninline = entry.noninline;
      out._columns.push_back(col);
    }

    out._ranges.reserve(view.rangeCount());
    for (std::size_t i = 0; i < view.rangeCount(); ++i) {
      const blob::RangeEntry entry = view.range(i);
      out._ranges.push_back(RangeIndex{entry.firstColumn, entry.columnCount, entry.eraMask});
    }

    out._tables.reserve(view.tableCount());
    for (std::size_t i = 0; i < view.tableCount(); ++i) {
      const blob::TableEntry entry = view.table(i);
      out._tables.push_back(TableIndex{
        view.stringAt(entry.nameOff),
        view.stringAt(entry.diskNameOff),
        entry.firstRange,
        entry.rangeCount
      });
    }
    return out;
  }

#if WOWLIB_DB_SCHEMA_RUNTIME
  Result<SchemaCatalog> SchemaCatalog::fromBlob(std::vector<unsigned char> bytes) {
    const blob::View view{bytes};
    if (!view.valid())
      return makeError(ErrorCode::SchemaBlobInvalid,
                        "not a WDBS v1 schema blob (bad magic, version or " "section bounds)");
    // The span must reference the FINAL resting place of the bytes — move the
    // vector first, then view it inside _materialize's caller frame.
    std::vector<unsigned char> owned{std::move(bytes)};
    const std::span<const unsigned char> stable{owned};
    return _materialize(stable, std::move(owned));
  } Result<SchemaCatalog> SchemaCatalog::fromBlobFile(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in)
      return makeError(ErrorCode::IoError, "cannot open schema blob: " + path.string());
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (!in.good() && !in.eof())
      return makeError(ErrorCode::IoError, "cannot read schema blob: " + path.string());
    return fromBlob(std::move(bytes));
  }
#endif

  Result<std::size_t> SchemaCatalog::_eraIndexOf(ClientVersion version) const {
    for (std::size_t i = 0; i < _eras.size(); ++i)
      if (_eras[i].major == version.major) return i;
    return makeError(ErrorCode::UnsupportedClientVersion,
                      "no targeted era shares major version " + std::to_string(version.major));
  }

  Result<TableSchema> SchemaCatalog::lookup(std::string_view table, ClientVersion version) const {
    // Binary search the name-sorted directory (the blob writer sorts).
    std::size_t lo = 0;
    std::size_t hi = _tables.size();
    const TableIndex* found = nullptr;
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      const TableIndex& entry = _tables[mid];
      if (entry.name == table) {
        found = &entry;
        break;
      }
      if (entry.name < table) lo = mid + 1;
      else hi = mid;
    }
    if (found == nullptr)
      return makeError(ErrorCode::TableUnknown, "schema catalog knows no table '" + std::string{table} + '\'');
    return _eraIndexOf(version).and_then([&](std::size_t era) -> Result<TableSchema> {
      for (std::size_t r = 0; r < found->rangeCount; ++r) {
        const RangeIndex& range = _ranges[found->firstRange + r];
        if ((range.eraMask & (std::uint16_t{1} << era)) == 0) continue;
        return TableSchema{
          found->name,
          found->diskName,
          std::span<const Column>{_columns}.subspan(range.firstColumn, range.columnCount)
        };
      }
      return makeError(ErrorCode::UnsupportedClientVersion,
                        "table '" + std::string{found->name} + "' has no schema for the era of build " + std::to_string(
                          version.build));
    });
  }

#if WOWLIB_DB_SCHEMA_EMBEDDED
  namespace detail {
    /** The blob dbdgen baked into this build (see cmake/DbTables.cmake: the
        file lands in the build tree and --embed-dir points at it). Static
        storage — catalogs over it never copy. */
    constexpr unsigned char EmbeddedSchemaBlob[] = {
#embed <wowlib_schema.wdbs>
    };
    static_assert(blob::View{std::span<const unsigned char>{EmbeddedSchemaBlob}}.valid(),
                  "the embedded schema blob failed structural validation — "
                  "dbdgen output and schema_blob.hpp disagree on the format");
  } const SchemaCatalog& SchemaCatalog::embedded() {
    static const SchemaCatalog Catalog = _materialize(std::span<const unsigned char>{detail::EmbeddedSchemaBlob}, {});
    return Catalog;
  }
#endif
}
