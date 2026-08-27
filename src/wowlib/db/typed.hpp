#pragma once

/** @file
    Typed C++ access to the generic client-database engine: DECLARE YOUR OWN
    row struct, validated at COMPILE TIME against the same WoWDBDefs data the
    runtime engine uses.

    The struct names its table and client version like every generated record
    does (the @ref wowlib::db::TableRecord shape) and lists the columns it
    wants as ordinary members — a SUBSET is a projection:

    @code
    struct MapRow
    {
      static constexpr ClientVersion version = versions::wotlk;
      static constexpr std::string_view TableName = "Map";
      std::int32_t id = 0;
      std::string directory;   // any subset of the DBD's columns, by name
    };
    auto table = db::TypedTable<MapRow>::open();       // consteval-validated
    table->read(bytes);
    for (const MapRow& row : table->load()) use(row);
    auto ids = table->column<"id">();                  // zero-copy span
    @endcode

    Validation happens in constant evaluation against the `#embed`ed schema
    blob (schema_blob.hpp — the identical bytes @ref SchemaCatalog serves at
    runtime): every member must match a column of the struct's era BY NAME
    and BY SHAPE (value class, integer width and signedness, array length,
    locale count). A mismatch aborts compilation with a thrown diagnostic
    naming what disagreed. Member ROLES (id/relation/noninline) are not
    validated — the blob's full schema drives the codecs; the struct is only
    a projection over it. `writeBack` is consteval-gated to structs covering
    EVERY column (a projection cannot author a lossless table).

    Requires an `embedded`-capable build (`WOWLIB_DB_SCHEMA` embedded/both);
    a runtime-schema-only build has no bytes to validate against. */

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <wowlib/core/error.hpp>
#include <wowlib/db/dyn_table.hpp>
#include <wowlib/db/record_bridge.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/db/schema_blob.hpp>

#if !defined(WOWLIB_DB_SCHEMA_EMBEDDED) || !WOWLIB_DB_SCHEMA_EMBEDDED
#error "wowlib/db/typed.hpp needs the embedded schema blob \
(WOWLIB_DB_SCHEMA=embedded|both): the consteval validation reads it"
#endif

namespace wowlib::db {
  namespace detail {
    /** The blob, again, for CONSTANT EVALUATION in consumer TUs (the runtime
        catalog's copy lives inside schema_catalog.cpp). Same file, same
        bytes; consteval-only use. */
    inline constexpr unsigned char TypedSchemaBlob[] = {
#embed <wowlib_schema.wdbs>
    };

    /** The consteval-validation failure carrier: throwing it inside constant
        evaluation aborts compilation with the message in the diagnostic. */
    struct TypedMismatch {
      const char* message;
    };

    /** The blob's column list for (@a table, @a version)'s era, as
        blob-global column indexes [first, first+count), or a thrown
        @ref TypedMismatch. */
    consteval blob::RangeEntry typedRangeOf(std::string_view table, ClientVersion version) {
      const blob::View view{std::span<const unsigned char>{TypedSchemaBlob}};
      const auto at = view.findTable(table);
      if (!at) throw TypedMismatch{"the schema blob knows no table of this name"};
      std::size_t era = SIZE_MAX;
      for (std::size_t i = 0; i < view.eraCount(); ++i)
        if (view.era(i).major == version.major) era = i;
      if (era == SIZE_MAX)
        throw TypedMismatch{"no targeted era shares the struct's major version"};
      const blob::TableEntry entry = view.table(*at);
      for (std::size_t r = 0; r < entry.rangeCount; ++r) {
        const blob::RangeEntry range = view.range(entry.firstRange + r);
        if (range.eraMask & (std::uint16_t{1} << era)) return range;
      }
      throw TypedMismatch{"the table has no schema range covering the struct's era"};
    }

    /** The era-schema column index whose name is @a name, or a thrown
        @ref TypedMismatch. */
    consteval std::size_t typedColumnIndex(std::string_view table, ClientVersion version, std::string_view name) {
      const blob::View view{std::span<const unsigned char>{TypedSchemaBlob}};
      const blob::RangeEntry range = typedRangeOf(table, version);
      for (std::size_t c = 0; c < range.columnCount; ++c)
        if (view.copyStringAt(view.column(range.firstColumn + c).nameOff) == name) return c;
      throw TypedMismatch{"the era's schema has no column with this member's name"};
    }

    /** Validate @a Record against the blob (see the file note for the
        rules); also the projection builder's precondition. */
    template <typename Record>
    consteval bool typedValidate() {
      const blob::View view{std::span<const unsigned char>{TypedSchemaBlob}};
      const blob::RangeEntry range = typedRangeOf(Record::TableName, Record::Version);
      for (const Column& member : schemaOf<Record>()) {
        const std::size_t at = typedColumnIndex(Record::TableName, Record::Version, member.nameView());
        const blob::ColumnEntry col = view.column(range.firstColumn + at);
        if (col.type != member.type)
          throw TypedMismatch{
            "the member's value class (Int/Float/String/" "LocString) differs from the DBD column's"
          };
        if (member.type == ColumnType::Int && (col.bits != member.bits || col.isSigned != member.isSigned))
          throw TypedMismatch{"the member's integer width or signedness " "differs from the DBD column's"};
        if (col.arrayLen != member.arrayLen)
          throw TypedMismatch{"the member's array length differs from the DBD column's"};
        if (member.type == ColumnType::LocString && col.localeCount != member.localeCount)
          throw TypedMismatch{"the member's LocString locale count differs " "from the DBD column's (wrong era?)"};
      }
      return true;
    }

    /** The projection map of @a Record: member position -> era-schema column
        index, as a static (never-dangling) array. */
    template <typename Record>
    consteval auto typedProjection() {
      std::vector<std::size_t> map;
      for (const Column& member : schemaOf<Record>())
        map.push_back(typedColumnIndex(Record::TableName, Record::Version, member.nameView()));
      return std::define_static_array(map);
    }

    /** A compile-time string NTTP (the `column<"id">()` spelling). */
    template <std::size_t N>
    struct FixedString {
      char text[N] = {};
      consteval FixedString(const char (&s)[N]) { std::copy_n(s, N, text); }
      consteval std::string_view view() const { return {text, N - 1}; }
    };
  }

  /** A typed, consteval-validated face over one @ref DynTable: the table
      itself stays fully generic (every column decodes, byte-perfect
      round-trip holds); this projects rows into @a Record values, copies
      edits back, and hands out zero-copy typed column spans.
      @tparam Record a @ref TableRecord -shaped struct whose members name a
                     subset of the table's era columns. */
  template <TableRecord Record>
  class TypedTable {
    static_assert(detail::typedValidate<Record>());

  public:
    /** Open the era-resolved generic table with an empty row set.
        @return the typed face, or the catalog's resolution error. */
    static Result<TypedTable> open() {
      return DynTable::open(Record::TableName, Record::Version).transform([](DynTable&& table) {
        TypedTable out{};
        out._table = std::move(table);
        return out;
      });
    }

    /** The generic table under this face — decode/encode/validation and the
        generic cell accessors all live there; this class only TYPES rows. */
    DynTable& table() { return _table; }
    /** @copydoc table */
    const DynTable& table() const { return _table; }

    /** Decode a table file image (sugar for `table().read`). */
    Result<void> read(std::span<const std::byte> data) {
      return _table.read(data);
    }

    /** Serialize (sugar for `table().write`). */
    Result<FileBuffer> write(EncryptedPolicy policy = EncryptedPolicy::Preserve) const {
      return _table.write(policy);
    }

    /** Materialize every row as a @a Record — the projection copies only
        the declared columns, row-major and contiguous (the hot-loop shape).
        @return the rows. */
    std::vector<Record> load() const {
      std::vector < Record > out(_table.rowCount());
      ErasedRecordSink sink{out};
      static constexpr auto Schema = schemaOf<Record>();
      static constexpr auto Projection = detail::typedProjection<Record>();
      for (std::size_t r = 0; r < out.size(); ++r)
        for (std::size_t m = 0; m < Schema.size(); ++m) _copyCellIn(sink, r, m, Projection[m], Schema[m]);
      return out;
    }

    /** Replace the table's rows from @a records. Consteval-gated to FULL
        column coverage: a projection cannot author a lossless table.
        @param records the complete new row set. */
    void writeBack(const std::vector<Record>& records) requires(schemaOf<Record>().size() == detail::typedRangeOf(
      Record::TableName, Record::Version).columnCount) {
      _table.clearRows();
      const ErasedRecordSource source{records};
      static constexpr auto Schema = schemaOf<Record>();
      static constexpr auto Projection = detail::typedProjection<Record>();
      for (std::size_t r = 0; r < records.size(); ++r) {
        _table.appendRow();
        for (std::size_t m = 0; m < Schema.size(); ++m) _copyCellOut(source, r, m, Projection[m], Schema[m]);
      }
    }

    /** The zero-copy typed span of scalar column @a Name (rows x 1; array
        and string columns read through @ref load or the generic view). Valid
        until rows are added or removed.
        @tparam Name the member's name, e.g. `column<"id">()`.
        @return the live, read-only column span. */
    template <detail::FixedString Name>
    auto column() const {
      static constexpr auto Members = detail::recordMembers<Record>();
      constexpr std::size_t index = []() consteval {
        for (std::size_t i = 0; i < Members.size(); ++i)
          if (std::meta::identifier_of(Members[i]) == Name.view()) return i;
        throw detail::TypedMismatch{"no member of this record has that name"};
      }();
      using F = [:std::meta::type_of(Members[index]):];
      static_assert(std::is_arithmetic_v<F> && !std::is_same_v<F, bool>,
                    "column<\"name\">() serves scalar arithmetic columns; "
                    "load() or the generic accessors cover the rest");
      static constexpr auto Projection = detail::typedProjection<Record>();
      const auto view = _table.podColumn(Projection[index]);
      // add_const_t, not `const F`: gcc-16 drops a bare const applied to a
      // dependent splice alias (the span would come out span<F>).
      using CF = std::add_const_t<F>;
      CF* data = reinterpret_cast<CF*>(view->bytes.data());
      const std::size_t count = view->bytes.size() / sizeof(F);
      return std::span<CF>(data, count);
    }

  private:
    TypedTable() = default;

    /** One generic-table cell into the typed sink (member @a m of row @a r,
        schema column @a col). The value() calls cannot fail: the validation
        proved every kind and every index. */
    void _copyCellIn(ErasedRecordSink& sink,
                      std::size_t r,
                      std::size_t m,
                      std::size_t col,
                      const Column& shape) const {
      switch (shape.type) {
      case ColumnType::Int:
        for (std::size_t e = 0; e < shape.arrayLen; ++e) sink.setInt(r, m, e, _table.getInt(r, col, e).value());
        break;
      case ColumnType::Float:
        for (std::size_t e = 0; e < shape.arrayLen; ++e) sink.setFloat(r, m, e, _table.getFloat(r, col, e).value());
        break;
      case ColumnType::String:
        for (std::size_t e = 0; e < shape.arrayLen; ++e) sink.
          setString(r, m, e, _table.getString(r, col, e).value());
        break;
      case ColumnType::LocString:
        for (std::size_t e = 0; e < shape.localeCount; ++e) sink.setString(
          r, m, e, _table.getString(r, col, e).value());
        sink.setInt(r, m, 0, _table.locstringFlags(r, col).value());
        break;
      }
    }

    /** The mirror: one typed-source cell into the generic table. */
    void _copyCellOut(const ErasedRecordSource& source,
                       std::size_t r,
                       std::size_t m,
                       std::size_t col,
                       const Column& shape) {
      switch (shape.type) {
      case ColumnType::Int:
        for (std::size_t e = 0; e < shape.arrayLen; ++e) _table.setInt(r, col, source.getInt(r, m, e), e).value();
        break;
      case ColumnType::Float:
        for (std::size_t e = 0; e < shape.arrayLen; ++e)
          _table.setFloat(r, col, std::bit_cast<float>(source.getSlot(r, m, e)), e).value();
        break;
      case ColumnType::String:
        for (std::size_t e = 0; e < shape.arrayLen; ++e) _table.setString(r, col, source.getString(r, m, e), e).
                                                                 value();
        break;
      case ColumnType::LocString:
        for (std::size_t e = 0; e < shape.localeCount; ++e) _table.setString(r, col, source.getString(r, m, e), e).
                                                                    value();
        _table.setLocstringFlags(r, col, static_cast<std::uint32_t>(source.getInt(r, m, 0))).value();
        break;
      }
    }

    DynTable _table{};
  };
}
