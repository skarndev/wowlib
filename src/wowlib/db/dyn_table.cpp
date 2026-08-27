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
          record_bridge's consteval accessKindOf). */
      AccessKind kindOf(const Column& column) {
        switch (column.type) {
        case ColumnType::Float: return AccessKind::F32;
        case ColumnType::String: return AccessKind::Str;
        case ColumnType::LocString: return AccessKind::LocStr;
        case ColumnType::Int: break;
        }
        switch (column.bits) {
        case 8: return column.isSigned ? AccessKind::I8 : AccessKind::U8;
        case 16: return column.isSigned ? AccessKind::I16 : AccessKind::U16;
        case 64: return column.isSigned ? AccessKind::I64 : AccessKind::U64;
        default: return column.isSigned ? AccessKind::I32 : AccessKind::U32;
        }
      }

      /** Store @a v truncated to the exact element width, as the typed member
          assignment did (record_bridge.cpp's storeInt, minus the LocStr arm
          — the column store keeps flags out of the POD buffer). */
      void storePodInt(std::byte* p, AccessKind kind, std::int64_t v) {
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
          loadInt, minus the LocStr arm). */
      std::int64_t loadPodInt(const std::byte* p, AccessKind kind) {
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

    ColumnRows::ColumnRows(std::span<const Column> schema) : _schema{schema} {
      _facts.reserve(schema.size());
      _cols.resize(schema.size());
      for (std::size_t c = 0; c < schema.size(); ++c) {
        const Column& col = schema[c];
        Facts f{};
        f.kind = kindOf(col);
        switch (col.type) {
        case ColumnType::Int:
          f.elemBytes = static_cast<std::uint8_t>(col.bits / 8);
          f.podSlots = col.arrayLen;
          break;
        case ColumnType::Float:
          f.elemBytes = 4;
          f.podSlots = col.arrayLen;
          break;
        case ColumnType::String:
          f.strSlots = col.arrayLen;
          break;
        case ColumnType::LocString:
          f.strSlots = col.localeCount;
          f.hasFlags = true;
          break;
        }
        _facts.push_back(f);
        if (col.isId && _idColumn == SIZE_MAX) _idColumn = c;
      }
    }

    std::byte* ColumnRows::_podPtr(std::size_t record, std::size_t column, std::size_t element) {
      const Facts& f = _facts[column];
      return _cols[column].pod.data() + (record * f.podSlots + element) * f.elemBytes;
    }

    const std::byte* ColumnRows::_podPtr(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = _facts[column];
      return _cols[column].pod.data() + (record * f.podSlots + element) * f.elemBytes;
    }

    void ColumnRows::clear() {
      for (Store& s : _cols) {
        s.pod.clear();
        s.strs.clear();
        s.flags.clear();
      }
      _rows = 0;
    }

    void ColumnRows::reserve(std::size_t n) {
      for (std::size_t c = 0; c < _cols.size(); ++c) {
        const Facts& f = _facts[c];
        if (f.podSlots != 0) _cols[c].pod.reserve(n * f.podSlots * f.elemBytes);
        if (f.strSlots != 0) _cols[c].strs.reserve(n * f.strSlots);
        if (f.hasFlags) _cols[c].flags.reserve(n);
      }
    }

    std::size_t ColumnRows::add() {
      for (std::size_t c = 0; c < _cols.size(); ++c) {
        const Facts& f = _facts[c];
        if (f.podSlots != 0)
          _cols[c].pod.resize(_cols[c].pod.size() + std::size_t{f.podSlots} * f.elemBytes, std::byte{0});
        if (f.strSlots != 0) _cols[c].strs.resize(_cols[c].strs.size() + f.strSlots);
        if (f.hasFlags) _cols[c].flags.push_back(0);
      }
      return _rows++;
    }

    std::size_t ColumnRows::size() const { return _rows; }

    std::uint32_t ColumnRows::idOf(std::size_t record) const {
      if (_idColumn == SIZE_MAX) return 0;
      return static_cast<std::uint32_t>(loadPodInt(_podPtr(record, _idColumn, 0), _facts[_idColumn].kind));
    }

    void ColumnRows::setInt(std::size_t record, std::size_t column, std::size_t element, std::int64_t value) {
      const Facts& f = _facts[column];
      if (f.kind == AccessKind::LocStr) {
        _cols[column].flags[record] = static_cast<std::uint32_t>(value);
        return;
      }
      if (f.podSlots != 0 && f.kind != AccessKind::F32) storePodInt(_podPtr(record, column, element), f.kind, value);
      // float / string columns never receive setInt (record_bridge parity).
    }

    void ColumnRows::setFloat(std::size_t record, std::size_t column, std::size_t element, float value) {
      if (_facts[column].kind == AccessKind::F32) std::memcpy(_podPtr(record, column, element), &value, sizeof value);
    }

    void ColumnRows::setString(std::size_t record, std::size_t column, std::size_t element, std::string_view value) {
      const Facts& f = _facts[column];
      if (f.strSlots != 0) _cols[column].strs[record * f.strSlots + element] = std::string{value};
    }

    void ColumnRows::cloneWithId(std::size_t src, std::uint32_t newId) {
      const std::size_t fresh = add();
      for (std::size_t c = 0; c < _cols.size(); ++c) {
        const Facts& f = _facts[c];
        Store& s = _cols[c];
        if (f.podSlots != 0) {
          const std::size_t stride = std::size_t{f.podSlots} * f.elemBytes;
          std::memcpy(s.pod.data() + fresh * stride, s.pod.data() + src * stride, stride);
        }
        for (std::size_t e = 0; e < f.strSlots; ++e) s.strs[fresh * f.strSlots + e] = s.strs[src * f.strSlots + e];
        if (f.hasFlags) s.flags[fresh] = s.flags[src];
      }
      if (_idColumn != SIZE_MAX)
        storePodInt(_podPtr(fresh, _idColumn, 0), _facts[_idColumn].kind, newId);
    }

    std::size_t ColumnRows::findById(std::uint32_t id) const {
      for (std::size_t r = 0; r < _rows; ++r)
        if (idOf(r) == id) return r;
      return _rows;
    }

    std::int64_t ColumnRows::getInt(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = _facts[column];
      if (f.kind == AccessKind::LocStr) return _cols[column].flags[record];
      if (f.podSlots == 0 || f.kind == AccessKind::F32) return 0;
      // as the row-store bridge answered for non-int members
      return loadPodInt(_podPtr(record, column, element), f.kind);
    }

    std::uint32_t ColumnRows::getSlot(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = _facts[column];
      switch (f.kind) {
      case AccessKind::F32: {
        std::uint32_t x;
        std::memcpy(&x, _podPtr(record, column, element), sizeof x);
        return x;
      }
      case AccessKind::Str:
      case AccessKind::LocStr:
        return 0; // string refs are journaled, never slot-read
      default:
        return static_cast<std::uint32_t>(loadPodInt(_podPtr(record, column, element), f.kind));
      }
    }

    std::string_view ColumnRows::getString(std::size_t record, std::size_t column, std::size_t element) const {
      const Facts& f = _facts[column];
      if (f.strSlots == 0) return {};
      return _cols[column].strs[record * f.strSlots + element];
    }

    void ColumnRows::eraseRow(std::size_t record) {
      for (std::size_t c = 0; c < _cols.size(); ++c) {
        const Facts& f = _facts[c];
        Store& s = _cols[c];
        if (f.podSlots != 0) {
          const std::size_t stride = std::size_t{f.podSlots} * f.elemBytes;
          s.pod.erase(s.pod.begin() + static_cast<std::ptrdiff_t>(record * stride),
                      s.pod.begin() + static_cast<std::ptrdiff_t>((record + 1) * stride));
        }
        if (f.strSlots != 0)
          s.strs.erase(s.strs.begin() + static_cast<std::ptrdiff_t>(record * f.strSlots),
                       s.strs.begin() + static_cast<std::ptrdiff_t>((record + 1) * f.strSlots));
        if (f.hasFlags) s.flags.erase(s.flags.begin() + static_cast<std::ptrdiff_t>(record));
      }
      --_rows;
    }

    std::string& ColumnRows::stringSlot(std::size_t record, std::size_t column, std::size_t element) {
      return _cols[column].strs[record * _facts[column].strSlots + element];
    }

    std::uint32_t& ColumnRows::flagsSlot(std::size_t record, std::size_t column) {
      return _cols[column].flags[record];
    }

    std::uint32_t ColumnRows::flagsSlot(std::size_t record, std::size_t column) const {
      return _cols[column].flags[record];
    }
  }

  // --- DynTable -------------------------------------------------------------

#if WOWLIB_DB_SCHEMA_EMBEDDED
  Result<DynTable>
  DynTable::open(std::string_view table, ClientVersion version) {
    return SchemaCatalog::embedded().lookup(table, version).transform([&](const TableSchema& schema) {
      return fromSchema(schema, version);
    });
  }
#endif

  DynTable DynTable::fromSchema(const TableSchema& schema, ClientVersion version) {
    DynTable out{};
    out._rows = detail::ColumnRows{schema.columns};
    out._version = version;
    out._name = schema.name;
    out._diskName = schema.diskName;
    out._wire();
    return out;
  }

  DynTable::DynTable(const DynTable& other)
    : TableBase{other}, _rows{other._rows}, _version{other._version}, _name{other._name}, _diskName{other._diskName} {
    _wire();
  }

  DynTable& DynTable::operator=(const DynTable& other) {
    if (this == &other) return *this;
    TableBase::operator=(other);
    _rows = other._rows;
    _version = other._version;
    _name = other._name;
    _diskName = other._diskName;
    _wire();
    return *this;
  }

  DynTable::DynTable(DynTable&& other) noexcept
    : TableBase{std::move(other)},
      _rows{std::move(other._rows)},
      _version{other._version},
      _name{other._name},
      _diskName{other._diskName} {
    _wire();
  }

  DynTable& DynTable::operator=(DynTable&& other) noexcept {
    if (this == &other) return *this;
    TableBase::operator=(std::move(other));
    _rows = std::move(other._rows);
    _version = other._version;
    _name = other._name;
    _diskName = other._diskName;
    _wire();
    return *this;
  }

  Result<Column> DynTable::columnInfo(std::size_t column) const {
    const std::span<const Column> schema = _rows.schema();
    if (column >= schema.size())
      return makeError(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: column {} out of range ({} columns)", _name, column, schema.size()));
    return schema[column];
  }

  Result<std::size_t> DynTable::columnIndex(std::string_view name) const {
    const std::span<const Column> schema = _rows.schema();
    for (std::size_t c = 0; c < schema.size(); ++c)
      if (schema[c].nameView() == name) return c;
    return makeError(ErrorCode::TableUnknown, std::format("{}: no column named '{}'", _name, name));
  }

  Result<void> DynTable::eraseRow(std::size_t row) {
    if (row >= _rows.size())
      return makeError(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: row {} out of range ({} rows)", _name, row, _rows.size()));
    _rows.eraseRow(row);
    return {};
  }

  Result<std::size_t> DynTable::findById(std::uint32_t recordId) const {
    const std::size_t at = _rows.findById(recordId);
    if (at == _rows.size())
      return makeError(ErrorCode::TableUnknown, std::format("{}: no row with id {}", _name, recordId));
    return at;
  }

  Result<const detail::ColumnRows::Facts*> DynTable::_checkCell(std::size_t row,
                                                                std::size_t column,
                                                                std::size_t element,
                                                                ColumnType expected) const {
    const std::span<const Column> schema = _rows.schema();
    if (row >= _rows.size() || column >= schema.size())
      return makeError(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: cell ({}, {}) out of range ({} rows, " "{} columns)", _name, row, column,
                                    _rows.size(), schema.size()));
    const Column& col = schema[column];
    if (col.type != expected)
      return makeError(ErrorCode::SchemaMismatch, std::format("{}.{}: the column holds {}, not {}", _name,
                                                               col.nameView(), enumName(col.type),
                                                               enumName(expected)));
    const detail::ColumnRows::Facts& f = _rows.facts(column);
    const std::size_t slots = f.podSlots != 0 ? f.podSlots : std::size_t{f.strSlots};
    if (element >= slots)
      return makeError(ErrorCode::OffsetOutOfBounds,
                        std::format("{}.{}: element {} out of range ({} slots)", _name, col.nameView(), element,
                                    slots));
    return &f;
  }

  Result<std::int64_t> DynTable::getInt(std::size_t row, std::size_t column, std::size_t element) const {
    return _checkCell(row, column, element, ColumnType::Int).transform([&](auto) {
      return _rows.getInt(row, column, element);
    });
  }

  Result<void> DynTable::setInt(std::size_t row, std::size_t column, std::int64_t value, std::size_t element) {
    return _checkCell(row, column, element, ColumnType::Int).transform([&](auto) {
      _rows.setInt(row, column, element, value);
    });
  }

  Result<float> DynTable::getFloat(std::size_t row, std::size_t column, std::size_t element) const {
    return _checkCell(row, column, element, ColumnType::Float).transform([&](auto) {
      float out;
      std::memcpy(&out, _rows.podBytes(column).data() + (row * _rows.facts(column).podSlots + element) * 4,
                  sizeof out);
      return out;
    });
  }

  Result<void> DynTable::setFloat(std::size_t row, std::size_t column, float value, std::size_t element) {
    return _checkCell(row, column, element, ColumnType::Float).transform([&](auto) {
      _rows.setFloat(row, column, element, value);
    });
  }

  Result<std::string_view> DynTable::getString(std::size_t row, std::size_t column, std::size_t element) const {
    const auto str = _checkCell(row, column, element, ColumnType::String);
    if (str) return _rows.getString(row, column, element);
    // A LocString locale slot reads through the same accessor.
    return _checkCell(row, column, element, ColumnType::LocString).transform([&](auto) {
      return _rows.getString(row, column, element);
    });
  }

  Result<void> DynTable::setString(std::size_t row, std::size_t column, std::string_view value, std::size_t element) {
    const auto str = _checkCell(row, column, element, ColumnType::String);
    if (str) {
      _rows.setString(row, column, element, value);
      return {};
    }
    return _checkCell(row, column, element, ColumnType::LocString).transform([&](auto) {
      _rows.setString(row, column, element, value);
    });
  }

  Result<std::uint32_t> DynTable::locstringFlags(std::size_t row, std::size_t column) const {
    return _checkCell(row, column, 0, ColumnType::LocString).transform([&](auto) {
      return _rows.flagsSlot(row, column);
    });
  }

  Result<void> DynTable::setLocstringFlags(std::size_t row, std::size_t column, std::uint32_t flags) {
    return _checkCell(row, column, 0, ColumnType::LocString).transform([&](auto) {
      _rows.flagsSlot(row, column) = flags;
    });
  }

  Result<PodColumnView> DynTable::podColumn(std::size_t column) const {
    const std::span<const Column> schema = _rows.schema();
    if (column >= schema.size())
      return makeError(ErrorCode::OffsetOutOfBounds,
                        std::format("{}: column {} out of range ({} columns)", _name, column, schema.size()));
    const Column& col = schema[column];
    if (col.type != ColumnType::Int && col.type != ColumnType::Float)
      return makeError(ErrorCode::SchemaMismatch,
                        std::format("{}.{}: only numeric columns have a " "zero-copy view", _name, col.nameView()));
    const detail::ColumnRows::Facts& f = _rows.facts(column);
    PodColumnView out{};
    out.bytes = _rows.podBytes(column);
    out.elemBytes = f.elemBytes;
    out.elemsPerRow = f.podSlots;
    out.isSigned = col.isSigned;
    out.isFloat = col.type == ColumnType::Float;
    return out;
  }
}
