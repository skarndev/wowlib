/** @file
    The erased record bridge's field-access core: every kind-dispatching move
    between a codec value and a record member, compiled ONCE for all record
    types (see record_bridge.hpp for why). */

#include <wowlib/db/record_bridge.hpp>

#include <bit>
#include <cstring>

namespace wowlib::db
{
  namespace
  {
    using detail::AccessKind;
    using detail::ColumnAccess;
    using detail::RecordOps;

    /** The address of column @a a's @a element-th element inside record
        bytes @a rec. */
    std::byte* elem_ptr(std::byte* rec, const ColumnAccess& a, std::size_t element)
    {
      return rec + a.offset + element * a.elem_stride;
    }
    const std::byte* elem_ptr(const std::byte* rec, const ColumnAccess& a,
                              std::size_t element)
    {
      return rec + a.offset + element * a.elem_stride;
    }

    /** Store codec int @a v into the element, truncating exactly as the typed
        member assignment (`member = static_cast<E>(v)`) did. A LocStr takes
        the value as its flags field (the codec's `element == locale_count`
        convention rides along untouched — flags are not element-addressed). */
    void store_int(std::byte* rec, const ColumnAccess& a, std::size_t element,
                   std::int64_t v)
    {
      switch (a.kind)
      {
        case AccessKind::I8: {
          auto x = static_cast<std::int8_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::U8: {
          auto x = static_cast<std::uint8_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::I16: {
          auto x = static_cast<std::int16_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::U16: {
          auto x = static_cast<std::uint16_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::I32: {
          auto x = static_cast<std::int32_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::U32: {
          auto x = static_cast<std::uint32_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::I64: {
          auto x = static_cast<std::int64_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::U64: {
          auto x = static_cast<std::uint64_t>(v);
          std::memcpy(elem_ptr(rec, a, element), &x, sizeof x);
          break;
        }
        case AccessKind::LocStr: {
          auto x = static_cast<std::uint32_t>(v);
          std::memcpy(rec + a.flags_offset, &x, sizeof x);
          break;
        }
        case AccessKind::F32:
        case AccessKind::Str:
          break;  // float / string members never receive set_int (as before).
      }
    }

    /** Read the element back as the codec's int64 (LocStr → flags). */
    std::int64_t load_int(const std::byte* rec, const ColumnAccess& a,
                          std::size_t element)
    {
      const std::byte* p = elem_ptr(rec, a, element);
      switch (a.kind)
      {
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
          std::memcpy(&x, rec + a.flags_offset, sizeof x);
          return x;
        }
        case AccessKind::F32:
        case AccessKind::Str:
          return 0;  // as the typed bridge answered for non-int members.
      }
      return 0;
    }

    /** The std::string element of a Str/LocStr column (LocStr strings are
        element-addressed; its flags never come through here). */
    std::string& string_at(std::byte* rec, const ColumnAccess& a, std::size_t element)
    {
      return *reinterpret_cast<std::string*>(elem_ptr(rec, a, element));
    }
    const std::string& string_at(const std::byte* rec, const ColumnAccess& a,
                                 std::size_t element)
    {
      return *reinterpret_cast<const std::string*>(elem_ptr(rec, a, element));
    }

    /** The record's id via its $id$ column (0 when the schema has none —
        the typed bridge's behavior). */
    std::uint32_t id_of_record(const RecordOps& ops, const std::byte* rec)
    {
      if (ops.id_column == SIZE_MAX)
        return 0;
      return static_cast<std::uint32_t>(
        load_int(rec, ops.columns[ops.id_column], 0));
    }
  }

  // --- ErasedRecordSink -----------------------------------------------------

  void ErasedRecordSink::clear() { ops_->clear(vec_); }
  void ErasedRecordSink::reserve(std::size_t n) { ops_->reserve(vec_, n); }
  std::size_t ErasedRecordSink::add() { return ops_->add(vec_); }
  std::size_t ErasedRecordSink::size() const { return ops_->size(vec_); }

  std::uint32_t ErasedRecordSink::id_of(std::size_t record) const
  {
    return id_of_record(*ops_, ops_->cat(vec_, record));
  }

  void ErasedRecordSink::set_int(std::size_t record, std::size_t column,
                                 std::size_t element, std::int64_t value)
  {
    store_int(ops_->at(vec_, record), ops_->columns[column], element, value);
  }

  void ErasedRecordSink::set_float(std::size_t record, std::size_t column,
                                   std::size_t element, float value)
  {
    const ColumnAccess& a = ops_->columns[column];
    if (a.kind == AccessKind::F32)
      std::memcpy(elem_ptr(ops_->at(vec_, record), a, element), &value, sizeof value);
  }

  void ErasedRecordSink::set_string(std::size_t record, std::size_t column,
                                    std::size_t element, std::string_view value)
  {
    const ColumnAccess& a = ops_->columns[column];
    if (a.kind == AccessKind::Str || a.kind == AccessKind::LocStr)
      string_at(ops_->at(vec_, record), a, element) = std::string{value};
  }

  void ErasedRecordSink::clone_with_id(std::size_t src, std::uint32_t new_id)
  {
    ops_->clone_push(vec_, src);
    if (ops_->id_column != SIZE_MAX)
      store_int(ops_->at(vec_, ops_->size(vec_) - 1),
                ops_->columns[ops_->id_column], 0, new_id);
  }

  std::size_t ErasedRecordSink::find_by_id(std::uint32_t record_id) const
  {
    const std::size_t n = ops_->size(vec_);
    for (std::size_t i = 0; i < n; ++i)
      if (id_of_record(*ops_, ops_->cat(vec_, i)) == record_id)
        return i;
    return n;
  }

  // --- ErasedRecordSource ---------------------------------------------------

  std::size_t ErasedRecordSource::size() const { return ops_->size(vec_); }

  std::uint32_t ErasedRecordSource::id_of(std::size_t record) const
  {
    return id_of_record(*ops_, ops_->cat(vec_, record));
  }

  std::int64_t ErasedRecordSource::get_int(std::size_t record, std::size_t column,
                                           std::size_t element) const
  {
    return load_int(ops_->cat(vec_, record), ops_->columns[column], element);
  }

  std::uint32_t ErasedRecordSource::get_slot(std::size_t record, std::size_t column,
                                             std::size_t element) const
  {
    const ColumnAccess& a = ops_->columns[column];
    const std::byte* rec = ops_->cat(vec_, record);
    switch (a.kind)
    {
      case AccessKind::F32: {
        float f;
        std::memcpy(&f, elem_ptr(rec, a, element), sizeof f);
        return std::bit_cast<std::uint32_t>(f);
      }
      case AccessKind::Str:
      case AccessKind::LocStr:
        return 0;  // string refs are journaled, never slot-read (as before).
      default:
        return static_cast<std::uint32_t>(load_int(rec, a, element));
    }
  }

  std::string_view ErasedRecordSource::get_string(std::size_t record,
                                                  std::size_t column,
                                                  std::size_t element) const
  {
    const ColumnAccess& a = ops_->columns[column];
    if (a.kind != AccessKind::Str && a.kind != AccessKind::LocStr)
      return {};
    return string_at(ops_->cat(vec_, record), a, element);
  }
}
