#include <wowlib/db/schema_catalog.hpp>

#include <fstream>
#include <utility>

#include <wowlib/db/schema_blob.hpp>

namespace wowlib::db {
  SchemaCatalog SchemaCatalog::materialize(std::span<const unsigned char> bytes, std::vector<unsigned char> owned) {
    const blob::View view{bytes};
    SchemaCatalog out{};
    out.owned_ = std::move(owned);

    out.eras_.reserve(view.era_count());
    for (std::size_t i = 0; i < view.era_count(); ++i) out.eras_.push_back(view.era(i));

    out.columns_.reserve(view.column_count());
    for (std::size_t i = 0; i < view.column_count(); ++i) {
      const blob::ColumnEntry entry = view.column(i);
      Column col{};
      col.name = view.string_at(entry.name_off).data();
      col.type = entry.type;
      col.bits = entry.bits;
      col.is_signed = entry.is_signed;
      col.array_len = entry.array_len;
      col.locale_count = entry.locale_count;
      col.is_id = entry.is_id;
      col.is_relation = entry.is_relation;
      col.noninline = entry.noninline;
      out.columns_.push_back(col);
    }

    out.ranges_.reserve(view.range_count());
    for (std::size_t i = 0; i < view.range_count(); ++i) {
      const blob::RangeEntry entry = view.range(i);
      out.ranges_.push_back(RangeIndex{entry.first_column, entry.column_count, entry.era_mask});
    }

    out.tables_.reserve(view.table_count());
    for (std::size_t i = 0; i < view.table_count(); ++i) {
      const blob::TableEntry entry = view.table(i);
      out.tables_.push_back(TableIndex{
        view.string_at(entry.name_off),
        view.string_at(entry.disk_name_off),
        entry.first_range,
        entry.range_count
      });
    }
    return out;
  }

#if WOWLIB_DB_SCHEMA_RUNTIME
  Result<SchemaCatalog> SchemaCatalog::from_blob(std::vector<unsigned char> bytes) {
    const blob::View view{bytes};
    if (!view.valid())
      return make_error(ErrorCode::SchemaBlobInvalid,
                        "not a WDBS v1 schema blob (bad magic, version or " "section bounds)");
    // The span must reference the FINAL resting place of the bytes — move the
    // vector first, then view it inside materialize's caller frame.
    std::vector<unsigned char> owned{std::move(bytes)};
    const std::span<const unsigned char> stable{owned};
    return materialize(stable, std::move(owned));
  } Result<SchemaCatalog> SchemaCatalog::from_blob_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in)
      return make_error(ErrorCode::IoError, "cannot open schema blob: " + path.string());
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (!in.good() && !in.eof())
      return make_error(ErrorCode::IoError, "cannot read schema blob: " + path.string());
    return from_blob(std::move(bytes));
  }
#endif

  Result<std::size_t> SchemaCatalog::era_index_of(ClientVersion version) const {
    for (std::size_t i = 0; i < eras_.size(); ++i)
      if (eras_[i].major == version.major) return i;
    return make_error(ErrorCode::UnsupportedClientVersion,
                      "no targeted era shares major version " + std::to_string(version.major));
  }

  Result<TableSchema> SchemaCatalog::lookup(std::string_view table, ClientVersion version) const {
    // Binary search the name-sorted directory (the blob writer sorts).
    std::size_t lo = 0;
    std::size_t hi = tables_.size();
    const TableIndex* found = nullptr;
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      const TableIndex& entry = tables_[mid];
      if (entry.name == table) {
        found = &entry;
        break;
      }
      if (entry.name < table) lo = mid + 1;
      else hi = mid;
    }
    if (found == nullptr)
      return make_error(ErrorCode::TableUnknown, "schema catalog knows no table '" + std::string{table} + '\'');
    return era_index_of(version).and_then([&](std::size_t era) -> Result<TableSchema> {
      for (std::size_t r = 0; r < found->range_count; ++r) {
        const RangeIndex& range = ranges_[found->first_range + r];
        if ((range.era_mask & (std::uint16_t{1} << era)) == 0) continue;
        return TableSchema{
          found->name,
          found->disk_name,
          std::span<const Column>{columns_}.subspan(range.first_column, range.column_count)
        };
      }
      return make_error(ErrorCode::UnsupportedClientVersion,
                        "table '" + std::string{found->name} + "' has no schema for the era of build " + std::to_string(
                          version.build));
    });
  }

#if WOWLIB_DB_SCHEMA_EMBEDDED
  namespace detail {
    /** The blob dbdgen baked into this build (see cmake/DbTables.cmake: the
        file lands in the build tree and --embed-dir points at it). Static
        storage — catalogs over it never copy. */
    constexpr unsigned char embedded_schema_blob[] = {
#embed <wowlib_schema.wdbs>
    };
    static_assert(blob::View{std::span<const unsigned char>{embedded_schema_blob}}.valid(),
                  "the embedded schema blob failed structural validation — "
                  "dbdgen output and schema_blob.hpp disagree on the format");
  } const SchemaCatalog& SchemaCatalog::embedded() {
    static const SchemaCatalog catalog = materialize(std::span<const unsigned char>{detail::embedded_schema_blob}, {});
    return catalog;
  }
#endif
}
