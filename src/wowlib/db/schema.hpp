#pragma once

/** @file
    Schema reflection over generated client-database record structs: the
    column list, record stride and field counts the table engine and the
    per-magic codecs derive from a record type. The record's member TYPES carry
    the physical column shapes (intN_t width/signedness, float, std::string,
    LocString<N>, std::array<T, N> arrays); the db annotations carry the roles
    a type cannot (id / noninline / relation — annotations.hpp). */

#include <meta>

#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/annotations.hpp>
#include <wowlib/db/locstring.hpp>

namespace wowlib::db {
  /** The logical value class of a column. */
  enum class [[
      =welder::weld,
      =welder::doc("The logical value class of a table column.")
    ]] ColumnType : std::uint8_t {
    Int, /**< An integer column; Column::bits / Column::isSigned give the shape. */
    Float, /**< A 32-bit IEEE float column. */
    String, /**< A string-block reference column (u32 offset on disk). */
    LocString /**< A pre-Cataclysm localized string column (see LocString). */
  };

  /** One column of a record schema, derived from a record member by
      reflection (typed records) or from the schema catalog (the generic
      table). Array columns are ONE Column with arrayLen > 1 — the WoWDBDefs
      view of the world, not the expanded on-disk field list. */
  struct [[
      =welder::weld,
      =welder::doc("One column of a table schema: its name, value class, "
        "integer shape, array length and key roles.")
    ]] Column {
    [[=welder::mark::no_reassign,
      =welder::doc("The column name (WoWDBDefs spelling).")]]
    const char* name = nullptr; /**< The member spelling (interned, never dangling). */
    [[=welder::doc("The logical value class.")]]
    ColumnType type = ColumnType::Int;
    [[=welder::doc("Integer element width in bits; 32 for float/string refs.")]]
    std::uint8_t bits = 0;
    [[=welder::doc("Integer signedness; false for non-Int columns.")]]
    bool isSigned = false;
    [[=welder::doc("Element count; 1 for scalar columns.")]]
    std::uint16_t arrayLen = 1;
    [[=welder::doc("LocString language slots (8 or 16); 0 otherwise.")]]
    std::uint8_t localeCount = 0;
    [[=welder::doc("Whether this is the table's primary key ($id$).")]]
    bool isId = false;
    [[=welder::doc("Whether this is a relationship key ($relation$).")]]
    bool isRelation = false;
    [[=welder::doc("Whether the column holds no bytes inside the record "
      "image ($noninline$).")]]
    bool noninline = false;

    /** User-declared (defaulted) so Column is NOT an aggregate: bindings
        would otherwise synthesize a field-wise constructor, and its
        `const char* name` parameter would store a pointer into the
        marshalling temporary — Column is read-only schema metadata, never
        constructed from a bound language. */
    Column() = default;

    /** The member spelling as a view.
        @return the interned column name. */
    constexpr std::string_view nameView() const { return name; }

    /** The bytes the column occupies inside a fixed-stride (WDBC/WDB2) record
        image: zero when noninline, the flags field included for LocString.
        @return the on-disk byte width. */
    constexpr std::size_t inlineBytes() const {
      if (noninline) return 0;
      switch (type) {
      case ColumnType::Int: return static_cast<std::size_t>(bits) / 8 * arrayLen;
      case ColumnType::Float:
      case ColumnType::String: return std::size_t{4} * arrayLen;
      case ColumnType::LocString: return (static_cast<std::size_t>(localeCount) + 1) * 4;
      }
      return 0;
    }

    /** The expanded on-disk field slots the column contributes to a WDBC/WDB2
        header field count: arrays count each element, LocString counts every
        language slot plus the flags field, noninline columns count zero.
        @return the field slot count. */
    constexpr std::uint32_t fieldSlots() const {
      if (noninline) return 0;
      if (type == ColumnType::LocString) return static_cast<std::uint32_t>(localeCount) + 1;
      return arrayLen;
    }

    /** The string-block references the column stores per record (the width of
        the engine's per-record offset journal).
        @return String columns: the element count; LocString: the language slot
                count; 0 otherwise. */
    constexpr std::size_t stringSlots() const {
      if (noninline) return 0;
      if (type == ColumnType::String) return arrayLen;
      if (type == ColumnType::LocString) return localeCount;
      return 0;
    }
  };

  namespace detail {
    /** Trait: is @a T a LocString<N>? Exposes the language slot count. */
    template <typename T>
    struct LocstringTraits {
      static constexpr bool Value = false;
    };

    template <std::size_t N>
    struct LocstringTraits<LocString<N>> {
      static constexpr bool Value = true;
      static constexpr std::size_t Langs = N;
    };

    /** Trait: the element type / extent of a member — unwraps std::array. */
    template <typename T>
    struct ElementTraits {
      using Element = T;
      static constexpr std::size_t Extent = 1;
      static constexpr bool IsArray = false;
    };

    template <typename T, std::size_t N>
    struct ElementTraits<std::array<T, N>> {
      using Element = T;
      static constexpr std::size_t Extent = N;
      static constexpr bool IsArray = true;
    };

    /** Whether every byte of @a bytes is zero (an empty span counts as zero).
        @param bytes the span to test.
        @return true when all bytes are zero. */
    inline bool allZero(std::span<const std::byte> bytes) {
      return std::ranges::all_of(bytes, [](std::byte b) {
        return b == std::byte{0};
      });
    }

    /** The first annotation of type @a Spec on reflected member @a M, if any.
        @tparam Spec the annotation payload type (a `*_spec` struct).
        @tparam M    the reflected member.
        @return the payload, or nullopt when the member is unannotated. */
    template <typename Spec, std::meta::info M>
    consteval std::optional<Spec> annotation() {
      auto anns = std::meta::annotations_of_with_type(M, ^^Spec);
      if (anns.empty()) return std::nullopt;
      return std::meta::extract<Spec>(anns[0]);
    }

    /** The physical column shape of a record member of type @a Member — the
        name and the annotation-carried roles are the caller's to fill in.
        @tparam Member the member's declared type.
        @return the partially-filled Column. */
    template <typename Member>
    consteval Column classifyMember() {
      using Elem = typename ElementTraits<Member>::Element;
      Column col{};
      col.arrayLen = static_cast<std::uint16_t>(ElementTraits<Member>::Extent);
      if constexpr (LocstringTraits<Member>::Value) {
        col.type = ColumnType::LocString;
        col.bits = 32;
        col.localeCount = static_cast<std::uint8_t>(LocstringTraits<Member>::Langs);
      }
      else if constexpr (std::same_as<Elem, std::string>) {
        col.type = ColumnType::String;
        col.bits = 32;
      }
      else if constexpr (std::same_as<Elem, float>) {
        col.type = ColumnType::Float;
        col.bits = 32;
      }
      else if constexpr (std::integral<Elem> && !std::same_as<Elem, bool>) {
        col.type = ColumnType::Int;
        col.bits = static_cast<std::uint8_t>(sizeof(Elem) * 8);
        col.isSigned = std::is_signed_v<Elem>;
      }
      else static_assert(false, "unsupported record column type");
      return col;
    }

    /** The reflected non-static data members of record @a Record, declaration
        order. Generated records are flat structs — no base flattening.
        @tparam Record the record type.
        @return a static array of the reflected members. */
    template <typename Record>
    consteval auto recordMembers() {
      std::vector<std::meta::info> out;
      for (auto m : std::meta::nonstatic_data_members_of(^^Record, std::meta::access_context::unchecked())) out.
        push_back(m);
      return std::define_static_array(out);
    }
  }

  /** The column schema of record @a Record, derived by reflection: one Column
      per member, declaration order, with the annotation-carried roles applied.
      @tparam Record the record type.
      @return a static span of Columns, never dangling. */
  template <typename Record>
  consteval auto schemaOf() {
    std::vector<Column> cols;
    static constexpr auto Members = detail::recordMembers<Record>();
    template for (constexpr auto m : Members) {
      using M = [:std::meta::type_of(m):];
      Column col = detail::classifyMember<M>();
      col.name = std::define_static_string(std::meta::identifier_of(m));
      col.isId = detail::annotation<detail::IdSpec, m>().has_value();
      col.isRelation = detail::annotation<detail::RelationSpec, m>().has_value();
      col.noninline = detail::annotation<detail::NoninlineSpec, m>().has_value();
      cols.push_back(col);
    }
    return std::define_static_array(cols);
  }

  /** The fixed record stride of @a Record inside a WDBC/WDB2 record block.
      @tparam Record the record type.
      @return the on-disk record size in bytes. */
  template <typename Record>
  consteval std::size_t recordStride() {
    std::size_t bytes = 0;
    for (const Column& col : schemaOf<Record>()) bytes += col.inlineBytes();
    return bytes;
  }

  /** The expanded on-disk field slot count of @a Record (the WDBC/WDB2 header
      fieldCount a fresh write derives).
      @tparam Record the record type.
      @return the field slot count. */
  template <typename Record>
  consteval std::uint32_t fieldSlotCount() {
    std::uint32_t slots = 0;
    for (const Column& col : schemaOf<Record>()) slots += col.fieldSlots();
    return slots;
  }

  /** The string-block references one record of @a Record stores — the width of
      the engine's per-record original-offset journal.
      @tparam Record the record type.
      @return the per-record string slot count. */
  template <typename Record>
  consteval std::size_t stringSlotCount() {
    std::size_t slots = 0;
    for (const Column& col : schemaOf<Record>()) slots += col.stringSlots();
    return slots;
  }

  /** A type the table engine can carry as its record: a flat generated (or
      hand-written test) struct naming its client version and table. */
  template <typename R> concept TableRecord = std::is_class_v<R> && std::default_initializable<R> && requires {
    { R::Version } -> std::convertible_to<ClientVersion>; { std::string_view{R::TableName} };
  };
}
