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
      static constexpr std::string_view table_name = "Map";
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
    a projection over it. `write_back` is consteval-gated to structs covering
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
    inline constexpr unsigned char typed_schema_blob[] = {
#embed <wowlib_schema.wdbs>
    };

    /** The consteval-validation failure carrier: throwing it inside constant
        evaluation aborts compilation with the message in the diagnostic. */
    struct typed_mismatch {
      const char* message;
    };

    /** The blob's column list for (@a table, @a version)'s era, as
        blob-global column indexes [first, first+count), or a thrown
        @ref typed_mismatch. */
    consteval blob::RangeEntry typed_range_of(std::string_view table, ClientVersion version) {
      const blob::View view{std::span<const unsigned char>{typed_schema_blob}};
      const auto at = view.find_table(table);
      if (!at) throw typed_mismatch{"the schema blob knows no table of this name"};
      std::size_t era = SIZE_MAX;
      for (std::size_t i = 0; i < view.era_count(); ++i)
        if (view.era(i).major == version.major) era = i;
      if (era == SIZE_MAX)
        throw typed_mismatch{"no targeted era shares the struct's major version"};
      const blob::TableEntry entry = view.table(*at);
      for (std::size_t r = 0; r < entry.range_count; ++r) {
        const blob::RangeEntry range = view.range(entry.first_range + r);
        if (range.era_mask & (std::uint16_t{1} << era)) return range;
      }
      throw typed_mismatch{"the table has no schema range covering the struct's era"};
    }

    /** The era-schema column index whose name is @a name, or a thrown
        @ref typed_mismatch. */
    consteval std::size_t typed_column_index(std::string_view table, ClientVersion version, std::string_view name) {
      const blob::View view{std::span<const unsigned char>{typed_schema_blob}};
      const blob::RangeEntry range = typed_range_of(table, version);
      for (std::size_t c = 0; c < range.column_count; ++c)
        if (view.copy_string_at(view.column(range.first_column + c).name_off) == name) return c;
      throw typed_mismatch{"the era's schema has no column with this member's name"};
    }

    /** Validate @a Record against the blob (see the file note for the
        rules); also the projection builder's precondition. */
    template <typename Record>
    consteval bool typed_validate() {
      const blob::View view{std::span<const unsigned char>{typed_schema_blob}};
      const blob::RangeEntry range = typed_range_of(Record::table_name, Record::version);
      for (const Column& member : schema_of<Record>()) {
        const std::size_t at = typed_column_index(Record::table_name, Record::version, member.name_view());
        const blob::ColumnEntry col = view.column(range.first_column + at);
        if (col.type != member.type)
          throw typed_mismatch{
            "the member's value class (Int/Float/String/" "LocString) differs from the DBD column's"
          };
        if (member.type == ColumnType::Int && (col.bits != member.bits || col.is_signed != member.is_signed))
          throw typed_mismatch{"the member's integer width or signedness " "differs from the DBD column's"};
        if (col.array_len != member.array_len)
          throw typed_mismatch{"the member's array length differs from the DBD column's"};
        if (member.type == ColumnType::LocString && col.locale_count != member.locale_count)
          throw typed_mismatch{"the member's LocString locale count differs " "from the DBD column's (wrong era?)"};
      }
      return true;
    }

    /** The projection map of @a Record: member position -> era-schema column
        index, as a static (never-dangling) array. */
    template <typename Record>
    consteval auto typed_projection() {
      std::vector<std::size_t> map;
      for (const Column& member : schema_of<Record>())
        map.push_back(typed_column_index(Record::table_name, Record::version, member.name_view()));
      return std::define_static_array(map);
    }

    /** A compile-time string NTTP (the `column<"id">()` spelling). */
    template <std::size_t N>
    struct fixed_string {
      char text[N] = {};
      consteval fixed_string(const char (&s)[N]) { std::copy_n(s, N, text); }
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
    static_assert(detail::typed_validate<Record>());

  public:
    /** Open the era-resolved generic table with an empty row set.
        @return the typed face, or the catalog's resolution error. */
    static Result<TypedTable> open() {
      return DynTable::open(Record::table_name, Record::version).transform([](DynTable&& table) {
        TypedTable out{};
        out.table_ = std::move(table);
        return out;
      });
    }

    /** The generic table under this face — decode/encode/validation and the
        generic cell accessors all live there; this class only TYPES rows. */
    DynTable& table() { return table_; }
    /** @copydoc table */
    const DynTable& table() const { return table_; }

    /** Decode a table file image (sugar for `table().read`). */
    Result<void> read(std::span<const std::byte> data) {
      return table_.read(data);
    }

    /** Serialize (sugar for `table().write`). */
    Result<FileBuffer> write(EncryptedPolicy policy = EncryptedPolicy::Preserve) const {
      return table_.write(policy);
    }

    /** Materialize every row as a @a Record — the projection copies only
        the declared columns, row-major and contiguous (the hot-loop shape).
        @return the rows. */
    std::vector<Record> load() const {
      std::vector < Record > out(table_.row_count());
      ErasedRecordSink sink{out};
      static constexpr auto schema = schema_of<Record>();
      static constexpr auto projection = detail::typed_projection<Record>();
      for (std::size_t r = 0; r < out.size(); ++r)
        for (std::size_t m = 0; m < schema.size(); ++m) copy_cell_in(sink, r, m, projection[m], schema[m]);
      return out;
    }

    /** Replace the table's rows from @a records. Consteval-gated to FULL
        column coverage: a projection cannot author a lossless table.
        @param records the complete new row set. */
    void write_back(const std::vector<Record>& records) requires(schema_of<Record>().size() == detail::typed_range_of(
      Record::table_name, Record::version).column_count) {
      table_.clear_rows();
      const ErasedRecordSource source{records};
      static constexpr auto schema = schema_of<Record>();
      static constexpr auto projection = detail::typed_projection<Record>();
      for (std::size_t r = 0; r < records.size(); ++r) {
        table_.append_row();
        for (std::size_t m = 0; m < schema.size(); ++m) copy_cell_out(source, r, m, projection[m], schema[m]);
      }
    }

    /** The zero-copy typed span of scalar column @a Name (rows x 1; array
        and string columns read through @ref load or the generic view). Valid
        until rows are added or removed.
        @tparam Name the member's name, e.g. `column<"id">()`.
        @return the live, read-only column span. */
    template <detail::fixed_string Name>
    auto column() const {
      static constexpr auto members = detail::record_members<Record>();
      constexpr std::size_t index = []() consteval {
        for (std::size_t i = 0; i < members.size(); ++i)
          if (std::meta::identifier_of(members[i]) == Name.view()) return i;
        throw detail::typed_mismatch{"no member of this record has that name"};
      }();
      using F = [:std::meta::type_of(members[index]):];
      static_assert(std::is_arithmetic_v<F> && !std::is_same_v<F, bool>,
                    "column<\"name\">() serves scalar arithmetic columns; "
                    "load() or the generic accessors cover the rest");
      static constexpr auto projection = detail::typed_projection<Record>();
      const auto view = table_.pod_column(projection[index]);
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
    void copy_cell_in(ErasedRecordSink& sink,
                      std::size_t r,
                      std::size_t m,
                      std::size_t col,
                      const Column& shape) const {
      switch (shape.type) {
      case ColumnType::Int:
        for (std::size_t e = 0; e < shape.array_len; ++e) sink.set_int(r, m, e, table_.get_int(r, col, e).value());
        break;
      case ColumnType::Float:
        for (std::size_t e = 0; e < shape.array_len; ++e) sink.set_float(r, m, e, table_.get_float(r, col, e).value());
        break;
      case ColumnType::String:
        for (std::size_t e = 0; e < shape.array_len; ++e) sink.
          set_string(r, m, e, table_.get_string(r, col, e).value());
        break;
      case ColumnType::LocString:
        for (std::size_t e = 0; e < shape.locale_count; ++e) sink.set_string(
          r, m, e, table_.get_string(r, col, e).value());
        sink.set_int(r, m, 0, table_.locstring_flags(r, col).value());
        break;
      }
    }

    /** The mirror: one typed-source cell into the generic table. */
    void copy_cell_out(const ErasedRecordSource& source,
                       std::size_t r,
                       std::size_t m,
                       std::size_t col,
                       const Column& shape) {
      switch (shape.type) {
      case ColumnType::Int:
        for (std::size_t e = 0; e < shape.array_len; ++e) table_.set_int(r, col, source.get_int(r, m, e), e).value();
        break;
      case ColumnType::Float:
        for (std::size_t e = 0; e < shape.array_len; ++e)
          table_.set_float(r, col, std::bit_cast<float>(source.get_slot(r, m, e)), e).value();
        break;
      case ColumnType::String:
        for (std::size_t e = 0; e < shape.array_len; ++e) table_.set_string(r, col, source.get_string(r, m, e), e).
                                                                 value();
        break;
      case ColumnType::LocString:
        for (std::size_t e = 0; e < shape.locale_count; ++e) table_.set_string(r, col, source.get_string(r, m, e), e).
                                                                    value();
        table_.set_locstring_flags(r, col, static_cast<std::uint32_t>(source.get_int(r, m, 0))).value();
        break;
      }
    }

    DynTable table_{};
  };
}
