/** @file
    DynTable + ColumnRows bodies. The cell semantics here mirror
    record_bridge.cpp move for move — that file is the erased row-store twin
    of this column store, and the parity tests hold the two to byte-identical
    round-trips. */

#include <wowlib/db/dyn_table.hpp>

#include <cstring>
#include <format>
#include <utility>

namespace wowlib::db {
  namespace detail {
    namespace {
      /** The storage kind of @a column (the runtime twin of
          record_bridge's consteval access_kind_of). */
      AccessKind kind_of(const Column& column) {
        switch (column.type) {
        case ColumnType::Float: return AccessKind::F32;
        case ColumnType::String: return AccessKind::Str;
        case ColumnType::LocString: return AccessKind::LocStr;
        case ColumnType::Int: break;
        }
        switch (column.bits) {
        case 8: return column.is_signed ? AccessKind::I8 : AccessKind::U8;
        case 16: return column.is_signed ? AccessKind::I16 : AccessKind::U16;
        case 64: return column.is_signed ? AccessKind::I64 : AccessKind::U64;
        default: return column.is_signed ? AccessKind::I32 : AccessKind::U32;
        }
      }

      /** Store @a v truncated to the exact element width, as the typed member
          assignment did (record_bridge.cpp's store_int, minus the LocStr arm
          — the column store keeps flags out of the POD buffer). */
      void store_pod_int(std::byte* p, AccessKind kind, std::int64_t v) {
        switch (kind) {
        case AccessKind::I8: {
          auto x = static_cast<std::int8_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::U8: {
          auto x = static_cast<std::uint8_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::I16: {
          auto x = static_cast<std::int16_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::U16: {
          auto x = static_cast<std::uint16_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::I32: {
          auto x = static_cast<std::int32_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::U32: {
          auto x = static_cast<std::uint32_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::I64: {
          auto x = static_cast<std::int64_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::U64: {
          auto x = static_cast<std::uint64_t>(v);
          std::memcpy(p, &x, sizeof x);
          break;
        }
        case AccessKind::F32:
        case AccessKind::Str:
        case AccessKind::LocStr:
          break; // never POD-int-stored
        }
      }

      /** Load the element back as the codec's int64 (record_bridge.cpp's
          load_int, minus the LocStr arm). */
      std::int64_t load_pod_int(const std::byte* p, AccessKind kind) {
        switch (kind) {
        case AccessKind::I8: {
          std::int8_t x;
          std::memcpy(&x, p, sizeof x);
          return x;
        }
        case AccessKind::U8: {
          std::uint8_t x;
          std::memcpy(&x, p, sizeof x);
          return x;
        }
        case AccessKind::I16: {
          std::int16_t x;
          std::memcpy(&x, p, sizeof x);
          return x;
        }
        case AccessKind::U16: {
          std::uint16_t x;
          std::memcpy(&x, p, sizeof x);
          return x;
        }
        case AccessKind::I32: {
          std::int32_t x;
          std::memcpy(&x, p, sizeof x);
          return x;
        }
        case AccessKind::U32: {
          std::uint32_t x;
          std::memcpy(&x, p, sizeof x);
          return x;
        }
        case AccessKind::I64:
        case AccessKind::U64: {
          std::int64_t x;
          std::memcpy(&x, p, sizeof x);
          return x;
        }
        case AccessKind::F32:
        case AccessKind::Str:
        case AccessKind::LocStr:
          return 0;
        }
        return 0;
      }
    }

    ColumnRows::ColumnRows(std::span<const Column> schema) : schema_{schema} {
      facts_.reserve(schema.size());
      cols_.resize(schema.size());
      for (std::size_t c = 0; c < schema.size(); ++c) {
        const Column& col = schema[c];
        Facts f{};
        f.kind = kind_of(col);
        switch (col.type) {
        case ColumnType::Int:
          f.elem_bytes = static_cast<std::uint8_t>(col.bits / 8);
          f.pod_slots = col.array_len;
          break;
        case ColumnType::Float:
          f.elem_bytes = 4;
          f.pod_slots = col.array_len;
          break;
        case ColumnType::String:
          f.str_slots = col.array_len;
          break;
        case ColumnType::LocString:
          f.str_slots = col.locale_count;
          f.has_flags = true;
          break;
        }
        facts_.push_back(f);
        if (col.is_id && id_column_ == SIZE_MAX) id_column_ = c;
      }
    }

    std::byte* ColumnRows::pod_ptr(std::size_t record, std::size_t column, std::size_t element) {
      const Facts& f = facts_[column];
      return cols_[column].pod.data() + (record * f.pod_slots + element) * f.elem_bytes;
    }

    const std::byte* ColumnRows::pod_ptr(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = facts_[column];
      return cols_[column].pod.data() + (record * f.pod_slots + element) * f.elem_bytes;
    }

    void ColumnRows::clear() {
      for (Store& s : cols_) {
        s.pod.clear();
        s.strs.clear();
        s.flags.clear();
      }
      rows_ = 0;
    }

    void ColumnRows::reserve(std::size_t n) {
      for (std::size_t c = 0; c < cols_.size(); ++c) {
        const Facts& f = facts_[c];
        if (f.pod_slots != 0) cols_[c].pod.reserve(n * f.pod_slots * f.elem_bytes);
        if (f.str_slots != 0) cols_[c].strs.reserve(n * f.str_slots);
        if (f.has_flags) cols_[c].flags.reserve(n);
      }
    }

    std::size_t ColumnRows::add() {
      for (std::size_t c = 0; c < cols_.size(); ++c) {
        const Facts& f = facts_[c];
        if (f.pod_slots != 0)
          cols_[c].pod.resize(cols_[c].pod.size() + std::size_t{f.pod_slots} * f.elem_bytes, std::byte{0});
        if (f.str_slots != 0) cols_[c].strs.resize(cols_[c].strs.size() + f.str_slots);
        if (f.has_flags) cols_[c].flags.push_back(0);
      }
      return rows_++;
    }

    std::size_t ColumnRows::size() const { return rows_; }

    std::uint32_t ColumnRows::id_of(std::size_t record) const {
      if (id_column_ == SIZE_MAX) return 0;
      return static_cast<std::uint32_t>(load_pod_int(pod_ptr(record, id_column_, 0), facts_[id_column_].kind));
    }

    void ColumnRows::set_int(std::size_t record, std::size_t column, std::size_t element, std::int64_t value) {
      const Facts& f = facts_[column];
      if (f.kind == AccessKind::LocStr) {
        cols_[column].flags[record] = static_cast<std::uint32_t>(value);
        return;
      }
      if (f.pod_slots != 0 && f.kind != AccessKind::F32) store_pod_int(pod_ptr(record, column, element), f.kind, value);
      // float / string columns never receive set_int (record_bridge parity).
    }

    void ColumnRows::set_float(std::size_t record, std::size_t column, std::size_t element, float value) {
      if (facts_[column].kind == AccessKind::F32) std::memcpy(pod_ptr(record, column, element), &value, sizeof value);
    }

    void ColumnRows::set_string(std::size_t record, std::size_t column, std::size_t element, std::string_view value) {
      const Facts& f = facts_[column];
      if (f.str_slots != 0) cols_[column].strs[record * f.str_slots + element] = std::string{value};
    }

    void ColumnRows::clone_with_id(std::size_t src, std::uint32_t new_id) {
      const std::size_t fresh = add();
      for (std::size_t c = 0; c < cols_.size(); ++c) {
        const Facts& f = facts_[c];
        Store& s = cols_[c];
        if (f.pod_slots != 0) {
          const std::size_t stride = std::size_t{f.pod_slots} * f.elem_bytes;
          std::memcpy(s.pod.data() + fresh * stride, s.pod.data() + src * stride, stride);
        }
        for (std::size_t e = 0; e < f.str_slots; ++e) s.strs[fresh * f.str_slots + e] = s.strs[src * f.str_slots + e];
        if (f.has_flags) s.flags[fresh] = s.flags[src];
      }
      if (id_column_ != SIZE_MAX)
        store_pod_int(pod_ptr(fresh, id_column_, 0), facts_[id_column_].kind, new_id);
    }

    std::size_t ColumnRows::find_by_id(std::uint32_t id) const {
      for (std::size_t r = 0; r < rows_; ++r)
        if (id_of(r) == id) return r;
      return rows_;
    }

    std::int64_t ColumnRows::get_int(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = facts_[column];
      if (f.kind == AccessKind::LocStr) return cols_[column].flags[record];
      if (f.pod_slots == 0 || f.kind == AccessKind::F32) return 0;
      // as the row-store bridge answered for non-int members
      return load_pod_int(pod_ptr(record, column, element), f.kind);
    }

    std::uint32_t ColumnRows::get_slot(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = facts_[column];
      switch (f.kind) {
      case AccessKind::F32: {
        std::uint32_t x;
        std::memcpy(&x, pod_ptr(record, column, element), sizeof x);
        return x;
      }
      case AccessKind::Str:
      case AccessKind::LocStr:
        return 0; // string refs are journaled, never slot-read
      default:
        return static_cast<std::uint32_t>(load_pod_int(pod_ptr(record, column, element), f.kind));
      }
    }

    std::string_view ColumnRows::get_string(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = facts_[column];
      if (f.str_slots == 0) return {};
      return cols_[column].strs[record * f.str_slots + element];
    }

    void ColumnRows::erase_row(std::size_t record) {
      for (std::size_t c = 0; c < cols_.size(); ++c) {
        const Facts& f = facts_[c];
        Store& s = cols_[c];
        if (f.pod_slots != 0) {
          const std::size_t stride = std::size_t{f.pod_slots} * f.elem_bytes;
          s.pod.erase(s.pod.begin() + static_cast<std::ptrdiff_t>(record * stride),
                      s.pod.begin() + static_cast<std::ptrdiff_t>((record + 1) * stride));
        }
        if (f.str_slots != 0)
          s.strs.erase(s.strs.begin() + static_cast<std::ptrdiff_t>(record * f.str_slots),
                       s.strs.begin() + static_cast<std::ptrdiff_t>((record + 1) * f.str_slots));
        if (f.has_flags) s.flags.erase(s.flags.begin() + static_cast<std::ptrdiff_t>(record));
      }
      --rows_;
    }

    std::string& ColumnRows::string_slot(std::size_t record, std::size_t column, std::size_t element) {
      return cols_[column].strs[record * facts_[column].str_slots + element];
    }

    std::uint32_t& ColumnRows::flags_slot(std::size_t record, std::size_t column) {
      return cols_[column].flags[record];
    }

    std::uint32_t ColumnRows::flags_slot(std::size_t record, std::size_t column) const {
      return cols_[column].flags[record];
    }
  }

  // --- DynTable -------------------------------------------------------------

#if WOWLIB_DB_SCHEMA_EMBEDDED
  Result<DynTable>
  DynTable::open(std::string_view table, ClientVersion version) {
    return SchemaCatalog::embedded().lookup(table, version).transform([&](const TableSchema& schema) {
      return from_schema(schema, version);
    });
  }
#endif

  DynTable DynTable::from_schema(const TableSchema& schema, ClientVersion version) {
    DynTable out{};
    out.rows_ = detail::ColumnRows{schema.columns};
    out.version_ = version;
    out.name_ = schema.name;
    out.disk_name_ = schema.disk_name;
    out.wire_();
    return out;
  }

  DynTable::DynTable(const DynTable& other)
    : TableBase{other}, rows_{other.rows_}, version_{other.version_}, name_{other.name_}, disk_name_{other.disk_name_} {
    wire_();
  }

  DynTable& DynTable::operator=(const DynTable& other) {
    if (this == &other) return *this;
    TableBase::operator=(other);
    rows_ = other.rows_;
    version_ = other.version_;
    name_ = other.name_;
    disk_name_ = other.disk_name_;
    wire_();
    return *this;
  }

  DynTable::DynTable(DynTable&& other) noexcept
    : TableBase{std::move(other)},
      rows_{std::move(other.rows_)},
      version_{other.version_},
      name_{other.name_},
      disk_name_{other.disk_name_} {
    wire_();
  }

  DynTable& DynTable::operator=(DynTable&& other) noexcept {
    if (this == &other) return *this;
    TableBase::operator=(std::move(other));
    rows_ = std::move(other.rows_);
    version_ = other.version_;
    name_ = other.name_;
    disk_name_ = other.disk_name_;
    wire_();
    return *this;
  }

  Result<Column> DynTable::column_info(std::size_t column) const {
    const std::span<const Column> schema = rows_.schema();
    if (column >= schema.size())
      return make_error(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: column {} out of range ({} columns)", name_, column, schema.size()));
    return schema[column];
  }

  Result<std::size_t> DynTable::column_index(std::string_view name) const {
    const std::span<const Column> schema = rows_.schema();
    for (std::size_t c = 0; c < schema.size(); ++c)
      if (schema[c].name_view() == name) return c;
    return make_error(ErrorCode::TableUnknown, std::format("{}: no column named '{}'", name_, name));
  }

  Result<void> DynTable::erase_row(std::size_t row) {
    if (row >= rows_.size())
      return make_error(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: row {} out of range ({} rows)", name_, row, rows_.size()));
    rows_.erase_row(row);
    return {};
  }

  Result<std::size_t> DynTable::find_by_id(std::uint32_t record_id) const {
    const std::size_t at = rows_.find_by_id(record_id);
    if (at == rows_.size())
      return make_error(ErrorCode::TableUnknown, std::format("{}: no row with id {}", name_, record_id));
    return at;
  }

  Result<const detail::ColumnRows::Facts*> DynTable::check_cell(std::size_t row,
                                                                std::size_t column,
                                                                std::size_t element,
                                                                ColumnType expected) const {
    const std::span<const Column> schema = rows_.schema();
    if (row >= rows_.size() || column >= schema.size())
      return make_error(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: cell ({}, {}) out of range ({} rows, " "{} columns)", name_, row, column,
                                    rows_.size(), schema.size()));
    const Column& col = schema[column];
    if (col.type != expected)
      return make_error(ErrorCode::SchemaMismatch, std::format("{}.{}: the column holds {}, not {}", name_,
                                                               col.name_view(), enum_name(col.type),
                                                               enum_name(expected)));
    const detail::ColumnRows::Facts& f = rows_.facts(column);
    const std::size_t slots = f.pod_slots != 0 ? f.pod_slots : std::size_t{f.str_slots};
    if (element >= slots)
      return make_error(ErrorCode::OffsetOutOfBounds,
                        std::format("{}.{}: element {} out of range ({} slots)", name_, col.name_view(), element,
                                    slots));
    return &f;
  }

  Result<std::int64_t> DynTable::get_int(std::size_t row, std::size_t column, std::size_t element) const {
    return check_cell(row, column, element, ColumnType::Int).transform([&](auto) {
      return rows_.get_int(row, column, element);
    });
  }

  Result<void> DynTable::set_int(std::size_t row, std::size_t column, std::int64_t value, std::size_t element) {
    return check_cell(row, column, element, ColumnType::Int).transform([&](auto) {
      rows_.set_int(row, column, element, value);
    });
  }

  Result<float> DynTable::get_float(std::size_t row, std::size_t column, std::size_t element) const {
    return check_cell(row, column, element, ColumnType::Float).transform([&](auto) {
      float out;
      std::memcpy(&out, rows_.pod_bytes(column).data() + (row * rows_.facts(column).pod_slots + element) * 4,
                  sizeof out);
      return out;
    });
  }

  Result<void> DynTable::set_float(std::size_t row, std::size_t column, float value, std::size_t element) {
    return check_cell(row, column, element, ColumnType::Float).transform([&](auto) {
      rows_.set_float(row, column, element, value);
    });
  }

  Result<std::string_view> DynTable::get_string(std::size_t row, std::size_t column, std::size_t element) const {
    const auto str = check_cell(row, column, element, ColumnType::String);
    if (str) return rows_.get_string(row, column, element);
    // A LocString locale slot reads through the same accessor.
    return check_cell(row, column, element, ColumnType::LocString).transform([&](auto) {
      return rows_.get_string(row, column, element);
    });
  }

  Result<void> DynTable::set_string(std::size_t row, std::size_t column, std::string_view value, std::size_t element) {
    const auto str = check_cell(row, column, element, ColumnType::String);
    if (str) {
      rows_.set_string(row, column, element, value);
      return {};
    }
    return check_cell(row, column, element, ColumnType::LocString).transform([&](auto) {
      rows_.set_string(row, column, element, value);
    });
  }

  Result<std::uint32_t> DynTable::locstring_flags(std::size_t row, std::size_t column) const {
    return check_cell(row, column, 0, ColumnType::LocString).transform([&](auto) {
      return rows_.flags_slot(row, column);
    });
  }

  Result<void> DynTable::set_locstring_flags(std::size_t row, std::size_t column, std::uint32_t flags) {
    return check_cell(row, column, 0, ColumnType::LocString).transform([&](auto) {
      rows_.flags_slot(row, column) = flags;
    });
  }

  Result<PodColumnView> DynTable::pod_column(std::size_t column) const {
    const std::span<const Column> schema = rows_.schema();
    if (column >= schema.size())
      return make_error(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: column {} out of range ({} columns)", name_, column, schema.size()));
    const Column& col = schema[column];
    if (col.type != ColumnType::Int && col.type != ColumnType::Float)
      return make_error(ErrorCode::SchemaMismatch,
                        std::format("{}.{}: only numeric columns have a " "zero-copy view", name_, col.name_view()));
    const detail::ColumnRows::Facts& f = rows_.facts(column);
    PodColumnView out{};
    out.bytes = rows_.pod_bytes(column);
    out.elem_bytes = f.elem_bytes;
    out.elems_per_row = f.pod_slots;
    out.is_signed = col.is_signed;
    out.is_float = col.type == ColumnType::Float;
    return out;
  }
}
