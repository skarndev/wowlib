#pragma once

/** @file
    The record bridge: ErasedRecordSink / ErasedRecordSource implement the
    non-templated RecordSink / RecordSource (codec.hpp) over a
    std::vector<Record> — with the field-access machinery compiled ONCE
    (record_bridge.cpp), not once per record type.

    The previous bridge (TypedRecordSink<Record>) reflection-expanded a
    member-dispatch chain per record type: six virtual methods times a
    `template for` over every member, times ~4200 bound (table x era) records.
    Measured on the Python bindings, that was ~128 KB of near-identical object
    code per binding shard — yet everything those chains did was fully
    determined by three facts per column: the member's byte OFFSET, its value
    KIND, and its element STRIDE. Those are data, not code. So the bridge now
    derives a static ColumnAccess table per record (consteval, from the same
    reflection schema_of() uses) and hands it to a shared non-templated core;
    what remains per record is that table plus a handful of one-line vector
    thunks (clear/reserve/add/at/clone) nothing can erase, because only they
    know sizeof(Record).

    Nothing about the public API changes: Table<Record> constructs the erased
    bridge from its records vector exactly as it constructed the typed one. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <meta>

#include <wowlib/db/codec.hpp>
#include <wowlib/db/schema.hpp>

namespace wowlib::db
{
  namespace detail
  {
    /** The storage kind of one record member, as the erased accessors see it:
        exact width and signedness for integers (the store must truncate the
        codec's int64 exactly as the typed member assignment did), float,
        std::string, or LocString. */
    enum class AccessKind : std::uint8_t
    {
      I8, U8, I16, U16, I32, U32, I64, U64,  /**< Integer elements, by shape. */
      F32,                                   /**< A float element. */
      Str,                                   /**< A std::string element. */
      LocStr                                 /**< A LocString<N>: strings at
                                                  offset, flags on the side. */
    };

    /** Everything the erased core needs to touch one column of a record:
        where it lives, what it is, and how its array elements stride. */
    struct ColumnAccess
    {
      std::uint32_t offset = 0;       /**< Member byte offset inside Record. */
      std::uint32_t flags_offset = 0; /**< LocStr only: byte offset of .flags. */
      std::uint16_t elem_stride = 0;  /**< Bytes between array elements. */
      AccessKind kind = AccessKind::U32; /**< The element's storage kind. */
    };

    /** The AccessKind of element type @a E (consteval; bool is rejected by
        classify_member long before this runs). */
    template <typename E>
    consteval AccessKind access_kind_of()
    {
      if constexpr (std::same_as<E, float>)
        return AccessKind::F32;
      else if constexpr (std::same_as<E, std::string>)
        return AccessKind::Str;
      else if constexpr (std::signed_integral<E>)
      {
        switch (sizeof(E))
        {
          case 1: return AccessKind::I8;
          case 2: return AccessKind::I16;
          case 4: return AccessKind::I32;
          default: return AccessKind::I64;
        }
      }
      else
      {
        switch (sizeof(E))
        {
          case 1: return AccessKind::U8;
          case 2: return AccessKind::U16;
          case 4: return AccessKind::U32;
          default: return AccessKind::U64;
        }
      }
    }

    /** The byte offset of @a Struct's member named @a name (consteval). */
    template <typename Struct>
    consteval std::uint32_t member_offset(std::string_view name)
    {
      for (auto m : std::meta::nonstatic_data_members_of(
             ^^Struct, std::meta::access_context::unchecked()))
        if (std::meta::identifier_of(m) == name)
          return static_cast<std::uint32_t>(std::meta::offset_of(m).bytes);
      return 0;
    }

    /** The per-column access table of @a Record: one ColumnAccess per member,
        declaration order — the same 1:1 correspondence schema_of() rests on. */
    template <typename Record>
    consteval auto column_access_of()
    {
      std::vector<ColumnAccess> out;
      static constexpr auto members = record_members<Record>();
      template for (constexpr auto m : members)
      {
        using M = [:std::meta::type_of(m):];
        using E = typename element_traits<M>::element;
        ColumnAccess a{};
        a.offset = static_cast<std::uint32_t>(std::meta::offset_of(m).bytes);
        if constexpr (locstring_traits<M>::value)
        {
          a.kind = AccessKind::LocStr;
          a.elem_stride = sizeof(std::string);
          a.flags_offset = a.offset + member_offset<M>("flags");
        }
        else
        {
          a.kind = access_kind_of<E>();
          a.elem_stride = sizeof(E);
        }
        out.push_back(a);
      }
      return std::define_static_array(out);
    }

    /** The one thing that stays per-record: a table of thunks over the record
        VECTOR (only they know sizeof(Record)) plus the access table above.
        Every thunk is a single vector call; the branchy field-access core they
        feed is compiled once in record_bridge.cpp. */
    struct RecordOps
    {
      std::span<const ColumnAccess> columns; /**< Per-column access facts. */
      std::size_t id_column = SIZE_MAX;      /**< Index of the $id$ column, or SIZE_MAX. */

      void (*clear)(void*) = nullptr;
      void (*reserve)(void*, std::size_t) = nullptr;
      std::size_t (*add)(void*) = nullptr;             /**< emplace_back() → index. */
      std::size_t (*size)(const void*) = nullptr;
      std::byte* (*at)(void*, std::size_t) = nullptr;  /**< &vec[i], as bytes. */
      const std::byte* (*cat)(const void*, std::size_t) = nullptr;
      void (*clone_push)(void*, std::size_t) = nullptr; /**< push_back(vec[src]). */
    };

    /** The $id$ column index of @a Record (consteval), or SIZE_MAX. */
    template <typename Record>
    consteval std::size_t id_column_of()
    {
      std::size_t i = 0;
      for (const Column& col : schema_of<Record>())
      {
        if (col.is_id)
          return i;
        ++i;
      }
      return SIZE_MAX;
    }

    /** The RecordOps instance of @a Record — the entire per-record residue. */
    template <typename Record>
    inline constexpr RecordOps record_ops{
      column_access_of<Record>(),
      id_column_of<Record>(),
      [](void* v) { static_cast<std::vector<Record>*>(v)->clear(); },
      [](void* v, std::size_t n) { static_cast<std::vector<Record>*>(v)->reserve(n); },
      [](void* v) {
        auto& vec = *static_cast<std::vector<Record>*>(v);
        vec.emplace_back();
        return vec.size() - 1;
      },
      [](const void* v) { return static_cast<const std::vector<Record>*>(v)->size(); },
      [](void* v, std::size_t i) {
        return reinterpret_cast<std::byte*>(
          &(*static_cast<std::vector<Record>*>(v))[i]);
      },
      [](const void* v, std::size_t i) {
        return reinterpret_cast<const std::byte*>(
          &(*static_cast<const std::vector<Record>*>(v))[i]);
      },
      [](void* v, std::size_t src) {
        auto& vec = *static_cast<std::vector<Record>*>(v);
        vec.push_back(vec[src]);
      }};
  }

  /** RecordSink over a live std::vector<Record>& — the decode target. The
      class is NOT a template: the templated constructor captures the record
      type's RecordOps, and every override lives in record_bridge.cpp,
      compiled once for all ~4200 bound record types. */
  class ErasedRecordSink final : public RecordSink
  {
  public:
    template <typename Record>
    explicit ErasedRecordSink(std::vector<Record>& records)
      : vec_{&records}, ops_{&detail::record_ops<Record>}
    {
    }

    void clear() override;
    void reserve(std::size_t n) override;
    std::size_t add() override;
    std::size_t size() const override;
    std::uint32_t id_of(std::size_t record) const override;

    void set_int(std::size_t record, std::size_t column, std::size_t element,
                 std::int64_t value) override;
    void set_float(std::size_t record, std::size_t column, std::size_t element,
                   float value) override;
    void set_string(std::size_t record, std::size_t column, std::size_t element,
                    std::string_view value) override;

    void clone_with_id(std::size_t src, std::uint32_t new_id) override;
    std::size_t find_by_id(std::uint32_t id) const override;

  private:
    void* vec_;
    const detail::RecordOps* ops_;
  };

  /** RecordSource over a const std::vector<Record>& — the encode source.
      Erased exactly like the sink. */
  class ErasedRecordSource final : public RecordSource
  {
  public:
    template <typename Record>
    explicit ErasedRecordSource(const std::vector<Record>& records)
      : vec_{&records}, ops_{&detail::record_ops<Record>}
    {
    }

    std::size_t size() const override;
    std::uint32_t id_of(std::size_t record) const override;

    std::int64_t get_int(std::size_t record, std::size_t column,
                         std::size_t element) const override;
    std::uint32_t get_slot(std::size_t record, std::size_t column,
                           std::size_t element) const override;
    std::string_view get_string(std::size_t record, std::size_t column,
                                std::size_t element) const override;

  private:
    const void* vec_;
    const detail::RecordOps* ops_;
  };
}
