/** @file
    The erased record bridge's field-access core: every kind-dispatching move
    between a codec value and a record member, compiled ONCE for all record
    types (see record_bridge.hpp for why). */

#include <wowlib/db/record_bridge.hpp>

#include <bit>
#include <cstring>

namespace wowlib::db {
  namespace {
    using detail::AccessKind;
    using detail::ColumnAccess;
    using detail::RecordOps;

    /** The address of column @a a's @a element-th element inside record
        bytes @a rec. */
    std::byte* elemPtr(std::byte* rec, const ColumnAccess& a, std::size_t element) {
      return rec + a.offset + element * a.elemStride;
    }

    const std::byte* elemPtr(const std::byte* rec, const ColumnAccess& a, std::size_t element) {
      return rec + a.offset + element * a.elemStride;
    }

    /** Store codec int @a v into the element, truncating exactly as the typed
        member assignment (`member = static_cast<E>(v)`) did. A LocStr takes
        the value as its flags field (the codec's `element == localeCount`
        convention rides along untouched — flags are not element-addressed). */
    void storeInt(std::byte* rec, const ColumnAccess& a, std::size_t element, std::int64_t v) {
      switch (a.kind) {
      case AccessKind::I8: {
        auto x = static_cast<std::int8_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::U8: {
        auto x = static_cast<std::uint8_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::I16: {
        auto x = static_cast<std::int16_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::U16: {
        auto x = static_cast<std::uint16_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::I32: {
        auto x = static_cast<std::int32_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::U32: {
        auto x = static_cast<std::uint32_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::I64: {
        auto x = static_cast<std::int64_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::U64: {
        auto x = static_cast<std::uint64_t>(v);
        std::memcpy(elemPtr(rec, a, element), &x, sizeof x);
        break;
      }
      case AccessKind::LocStr: {
        auto x = static_cast<std::uint32_t>(v);
        std::memcpy(rec + a.flagsOffset, &x, sizeof x);
        break;
      }
      case AccessKind::F32:
      case AccessKind::Str:
        break; // float / string members never receive setInt (as before).
      }
    }

    /** Read the element back as the codec's int64 (LocStr → flags). */
    std::int64_t loadInt(const std::byte* rec, const ColumnAccess& a, std::size_t element) {
      const std::byte* p = elemPtr(rec, a, element);
      switch (a.kind) {
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
      case AccessKind::LocStr: {
        std::uint32_t x;
        std::memcpy(&x, rec + a.flagsOffset, sizeof x);
        return x;
      }
      case AccessKind::F32:
      case AccessKind::Str:
        return 0; // as the typed bridge answered for non-int members.
      }
      return 0;
    }

    /** The std::string element of a Str/LocStr column (LocStr strings are
        element-addressed; its flags never come through here). */
    std::string& stringAt(std::byte* rec, const ColumnAccess& a, std::size_t element) {
      return *reinterpret_cast<std::string*>(elemPtr(rec, a, element));
    }

    const std::string& stringAt(const std::byte* rec, const ColumnAccess& a, std::size_t element) {
      return *reinterpret_cast<const std::string*>(elemPtr(rec, a, element));
    }

    /** The record's id via its $id$ column (0 when the schema has none —
        the typed bridge's behavior). */
    std::uint32_t idOfRecord(const RecordOps& ops, const std::byte* rec) {
      if (ops.idColumn == SIZE_MAX) return 0;
      return static_cast<std::uint32_t>(loadInt(rec, ops.columns[ops.idColumn], 0));
    }
  }

  // --- ErasedRecordSink -----------------------------------------------------

  void ErasedRecordSink::clear() { _ops->clear(_vec); }
  void ErasedRecordSink::reserve(std::size_t n) { _ops->reserve(_vec, n); }
  std::size_t ErasedRecordSink::add() { return _ops->add(_vec); }
  std::size_t ErasedRecordSink::size() const { return _ops->size(_vec); }

  std::uint32_t ErasedRecordSink::idOf(std::size_t record) const {
    return idOfRecord(*_ops, _ops->cat(_vec, record));
  }

  void ErasedRecordSink::setInt(std::size_t record, std::size_t column, std::size_t element, std::int64_t value) {
    storeInt(_ops->at(_vec, record), _ops->columns[column], element, value);
  }

  void ErasedRecordSink::setFloat(std::size_t record, std::size_t column, std::size_t element, float value) {
    const ColumnAccess& a = _ops->columns[column];
    if (a.kind == AccessKind::F32)
      std::memcpy(elemPtr(_ops->at(_vec, record), a, element), &value, sizeof value);
  }

  void ErasedRecordSink::setString(std::size_t record,
                                    std::size_t column,
                                    std::size_t element,
                                    std::string_view value) {
    const ColumnAccess& a = _ops->columns[column];
    if (a.kind == AccessKind::Str || a.kind == AccessKind::LocStr) stringAt(_ops->at(_vec, record), a, element) =
      std::string{value};
  }

  void ErasedRecordSink::cloneWithId(std::size_t src, std::uint32_t newId) {
    _ops->clonePush(_vec, src);
    if (_ops->idColumn != SIZE_MAX)
      storeInt(_ops->at(_vec, _ops->size(_vec) - 1), _ops->columns[_ops->idColumn], 0, newId);
  }

  std::size_t ErasedRecordSink::findById(std::uint32_t recordId) const {
    const std::size_t n = _ops->size(_vec);
    for (std::size_t i = 0; i < n; ++i)
      if (idOfRecord(*_ops, _ops->cat(_vec, i)) == recordId) return i;
    return n;
  }

  // --- ErasedRecordSource ---------------------------------------------------

  std::size_t ErasedRecordSource::size() const { return _ops->size(_vec); }

  std::uint32_t ErasedRecordSource::idOf(std::size_t record) const {
    return idOfRecord(*_ops, _ops->cat(_vec, record));
  }

  std::int64_t ErasedRecordSource::getInt(std::size_t record, std::size_t column, std::size_t element) const {
    return loadInt(_ops->cat(_vec, record), _ops->columns[column], element);
  }

  std::uint32_t ErasedRecordSource::getSlot(std::size_t record, std::size_t column, std::size_t element) const {
    const ColumnAccess& a = _ops->columns[column];
    const std::byte* rec = _ops->cat(_vec, record);
    switch (a.kind) {
    case AccessKind::F32: {
      float f;
      std::memcpy(&f, elemPtr(rec, a, element), sizeof f);
      return std::bit_cast<std::uint32_t>(f);
    }
    case AccessKind::Str:
    case AccessKind::LocStr:
      return 0; // string refs are journaled, never slot-read (as before).
    default:
      return static_cast<std::uint32_t>(loadInt(rec, a, element));
    }
  }

  std::string_view ErasedRecordSource::getString(std::size_t record, std::size_t column, std::size_t element) const {
    const ColumnAccess& a = _ops->columns[column];
    if (a.kind != AccessKind::Str && a.kind != AccessKind::LocStr) return {};
    return stringAt(_ops->cat(_vec, record), a, element);
  }
}
