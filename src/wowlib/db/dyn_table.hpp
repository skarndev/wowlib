#pragma once

/** @file
    DynTable — the generic, runtime-schema client-database table: the ONE
    table class every binding welds, replacing the ~8.5k generated per-era
    classes on the binding surface entirely.

    The schema arrives as DATA (@ref wowlib::db::SchemaCatalog — the WDBS
    blob dbdgen bakes), the rows live in a COLUMN STORE (one exact-width
    buffer per column — scan-friendly, and scalar columns are zero-copy
    viewable as (rows × elements) matrices), and the format engine is the
    same @ref wowlib::db::TableCore every typed table uses: the codecs only
    ever spoke the (record, column, element) sink/source protocol, so the
    column store implements @ref wowlib::db::RecordSink /
    @ref wowlib::db::RecordSource directly and byte-perfect round-trip is
    inherited, not re-earned.

    Cell semantics mirror the erased record bridge exactly (record_bridge.cpp
    is the reference): integers store truncated to the column's exact width
    and load back sign-correctly; a LocString column's FLAGS field travels as
    the codec's `setInt`/`getInt` on the column (never element-addressed);
    strings are element-addressed slots. The public accessors are stricter
    than the codec protocol — a kind mismatch or an out-of-range index is a
    designed error, not a silent no-op. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/db/record_bridge.hpp>
#include <wowlib/db/schema_catalog.hpp>
#include <wowlib/db/table_core.hpp>

namespace wowlib::db {
  namespace detail {
    /** The column-store row storage: one exact-width POD buffer per numeric
        column, one string vector per string-bearing column, one flags vector
        per LocString column — implementing both codec interfaces over them.
        Not welded; @ref wowlib::db::DynTable is the bound face. */
    class ColumnRows final : public RecordSink, public RecordSource {
    public:
      ColumnRows() = default;

      /** Bind the store to @a schema (whose lifetime the owner guarantees)
          and derive the per-column access facts.
          @param schema the resolved column list. */
      explicit ColumnRows(std::span<const Column> schema);

      // --- RecordSink -------------------------------------------------------
      void clear() override;
      void reserve(std::size_t n) override;
      std::size_t add() override;
      /** One override serves both interfaces (identical signatures). */
      std::size_t size() const override;
      std::uint32_t idOf(std::size_t record) const override;
      void setInt(std::size_t record, std::size_t column, std::size_t element, std::int64_t value) override;
      void setFloat(std::size_t record, std::size_t column, std::size_t element, float value) override;
      void setString(std::size_t record, std::size_t column, std::size_t element, std::string_view value) override;
      void cloneWithId(std::size_t src, std::uint32_t newId) override;
      std::size_t findById(std::uint32_t id) const override;

      // --- RecordSource -----------------------------------------------------
      std::int64_t getInt(std::size_t record, std::size_t column, std::size_t element) const override;
      std::uint32_t getSlot(std::size_t record, std::size_t column, std::size_t element) const override;
      std::string_view getString(std::size_t record, std::size_t column, std::size_t element) const override;

      // --- store operations beyond the codec protocol -----------------------
      /** Remove row @a record (all columns' slices). */
      void eraseRow(std::size_t record);

      /** Mutable string slot access (the public setter's target). */
      std::string& stringSlot(std::size_t record, std::size_t column, std::size_t element);

      /** The LocString flags of @a record in @a column. */
      std::uint32_t& flagsSlot(std::size_t record, std::size_t column);
      std::uint32_t flagsSlot(std::size_t record, std::size_t column) const;

      /** The per-column access facts, derived from the schema at bind. */
      struct Facts {
        AccessKind kind = AccessKind::U32; /**< The element storage kind. */
        std::uint8_t elemBytes = 0; /**< POD element width; 0 for strings. */
        std::uint16_t podSlots = 0; /**< POD elements per row (arrayLen). */
        std::uint16_t strSlots = 0; /**< String slots per row. */
        bool hasFlags = false; /**< LocString: a flags word per row. */
      };

      /** @return column @a column's derived facts. */
      const Facts& facts(std::size_t column) const { return _facts[column]; }

      /** The whole POD buffer of column @a column (row-major, exact width) —
          the zero-copy view the bindings expose as an array.
          @param column a numeric/float column.
          @return the raw bytes (`size() * podSlots * elemBytes`). */
      std::span<const std::byte> podBytes(std::size_t column) const {
        return _cols[column].pod;
      }

      /** @return the bound schema. */
      std::span<const Column> schema() const { return _schema; }

    private:
      /** One column's storage (only the vector matching its kind is used). */
      struct Store {
        std::vector<std::byte> pod; /**< Numeric/float elements. */
        std::vector<std::string> strs; /**< String / locale slots. */
        std::vector<std::uint32_t> flags; /**< LocString flags, one per row. */
      };

      /** The POD element pointer of (record, column, element). */
      std::byte* _podPtr(std::size_t record, std::size_t column, std::size_t element);
      const std::byte* _podPtr(std::size_t record, std::size_t column, std::size_t element) const;

      std::span<const Column> _schema{}; /**< Owner-guaranteed lifetime. */
      std::vector<Facts> _facts{}; /**< Per column, schema order. */
      std::vector<Store> _cols{}; /**< Per column, schema order. */
      std::size_t _rows = 0; /**< The row count. */
      std::size_t _idColumn = SIZE_MAX; /**< The $id$ column, or SIZE_MAX. */
    };
  }

  /** A scalar column's whole storage, for zero-copy array views: the raw
      exact-width buffer plus the facts to type it (rows x elements_per_row
      matrix, row-major). NOT welded, and deliberately not nested in DynTable
      — a welded class's public nested types are bound with it, and a span
      member cannot cross a binding boundary; the bindings build their
      array views (numpy etc.) from this by hand. */
  struct PodColumnView {
    std::span<const std::byte> bytes; /**< The buffer, row-major. */
    std::uint8_t elemBytes = 0; /**< element width in bytes. */
    std::uint16_t elemsPerRow = 1; /**< Array length (1 = scalar). */
    bool isSigned = false; /**< Integer signedness. */
    bool isFloat = false; /**< float32 elements. */
  };

  /** The generic client-database table: schema resolved at runtime from the
      schema catalog, rows in a column store, format engine and preserved
      decode state inherited from @ref TableBase (read/write/validate/strings/
      encryptedSections all bind once, there).

      Copy/move re-wire the inherited core at the new storage — the same
      contract every generated table honored. */
  class [[
      =welder::weld,
      =welder::weld_as("Table"),
      =welder::doc(R"(
        A client-database table with its schema resolved at RUNTIME from
        WoWDBDefs data: open any table of any targeted client era, read its
        rows, edit cells by (row, column, element), and write it back in the
        era's own format. Cells are addressed by column index (see
        column_index/column_info); scalar columns also expose zero-copy
        array views.)")
    ]] DynTable : public TableBase {
  public:
    /** An unwired table: every inherited operation reports
        InvalidEntityState until one is opened properly. */
    DynTable() = default;

#if WOWLIB_DB_SCHEMA_EMBEDDED
    /** Open a table over the EMBEDDED schema catalog (static lifetime — the
        resulting table is self-contained).
        @param table   the table identifier (e.g. "Map").
        @param version the client the schema resolves for.
        @return the empty, schema-bound table; TableUnknown /
                UnsupportedClientVersion when the catalog cannot resolve. */
    [[=welder::doc("Open an empty table by name, with its schema resolved "
        "for the given client version from the built-in WoWDBDefs "
        "data."),
      =welder::returns("the empty, schema-bound table; raises for an unknown "
        "table or an uncovered client era")]]
    static Result<DynTable> open(std::string_view table [[=welder::doc("the table name, e.g. \"Map\"")]],
                                 ClientVersion version [[=welder::doc("the client to resolve for")]]);
#endif

    /** Open a table over an explicitly resolved schema — the caller
        guarantees the schema's backing catalog outlives the table (the
        embedded catalog does so trivially; a runtime-loaded one is the
        caller's to keep).
        @param schema  the resolved schema views.
        @param version the client version the table serializes for.
        @return the empty, schema-bound table. */
    [[=welder::mark::exclude]]
    static DynTable fromSchema(const TableSchema& schema, ClientVersion version);

    DynTable(const DynTable& other);
    DynTable& operator=(const DynTable& other);
    DynTable(DynTable&& other) noexcept;
    DynTable& operator=(DynTable&& other) noexcept;
    ~DynTable() = default;

    /** @return the number of rows. */
    [[=welder::getter, =welder::doc("The number of rows.")]]
    std::size_t rowCount() const { return _rows.size(); }

    /** @return the number of schema columns. */
    [[=welder::getter, =welder::doc("The number of schema columns.")]]
    std::size_t columnCount() const { return _rows.schema().size(); }

    /** @return the resolved schema (owner-guaranteed lifetime). */
    [[=welder::mark::exclude]]
    std::span<const Column> schema() const { return _rows.schema(); }

    /** @return the client version the table serializes for. */
    [[=welder::getter, =welder::doc(
      "The client version the table serializes for.")]]
    ClientVersion version() const { return _version; }

    /** @return the identifier name the table was opened as. */
    [[=welder::getter, =welder::doc("The table name (WoWDBDefs identifier).")]]
    std::string_view name() const { return _name; }

    /** Column @a column's schema entry, by value (the welded metadata face
        of @ref schema).
        @param column the column index.
        @return the Column, or OffsetOutOfBounds. */
    [[=welder::doc("The schema entry of one column: name, value class, "
        "shape and key roles."),
      =welder::returns("the column description; raises when out of range")]]
    Result<Column> columnInfo(std::size_t column [[=welder::doc("the column index")]]) const;

    /** The index of column @a name.
        @param name the column name (case-sensitive, WoWDBDefs spelling).
        @return the index, or TableUnknown when the schema has no such column. */
    [[=welder::doc("The index of the column with the given WoWDBDefs name."),
      =welder::returns("the column index; raises when no column matches")]]
    Result<std::size_t> columnIndex(std::string_view name [[=welder::doc("the column name, case-sensitive")]]) const;

    /** Append a fresh zero/empty row.
        @return the new row's index. */
    [[=welder::doc("Append a fresh zero/empty row."),
      =welder::returns("the new row's index")]]
    std::size_t appendRow() { return _rows.add(); }

    /** Remove row @a row.
        @param row the row index.
        @return nothing; OffsetOutOfBounds when @a row is out of range. */
    [[=welder::doc("Remove one row."),
      =welder::returns("nothing; raises when the row is out of range")]]
    Result<void> eraseRow(std::size_t row [[=welder::doc("the row index")]]);

    /** Drop every row (schema and preserved decode state stay). */
    [[=welder::doc(
      "Drop every row (the schema and preserved decode state stay).")]]
    void clearRows() { _rows.clear(); }

    /** The first row whose $id$ equals @a id.
        @param id the primary key value.
        @return the row index; TableUnknown when no row matches or the schema
                has no $id$ column. */
    [[=welder::doc("The first row whose $id$ column equals the given key."),
      =welder::returns("the row index; raises when no row matches")]]
    Result<std::size_t> findById(std::uint32_t id [[=welder::doc("the primary key value")]]) const;

    /** Read an integer element. LocString columns answer their FLAGS word
        through @ref locstringFlags, not here.
        @param row     the row index.
        @param column  an Int column's index.
        @param element the array element (0 for scalars).
        @return the value, sign-extended from the column's exact width. */
    [[=welder::doc("Read an integer cell, sign-extended from the column's "
        "exact width. LocString flags read via locstring_flags."),
      =welder::returns("the value; raises on a non-Int column or an "
        "out-of-range index")]]
    Result<std::int64_t> getInt(std::size_t row [[=welder::doc("the row index")]],
                                 std::size_t column [[=welder::doc("an Int column's index")]],
                                 std::size_t element [[=welder::doc("the array element")]] = 0) const;

    /** Write an integer element (truncates to the column's exact width, as
        the typed member assignment did).
        @copydetails getInt */
    [[=welder::doc("Write an integer cell (truncates to the column's exact "
        "width, exactly as the typed member assignment did)."),
      =welder::returns("nothing; raises on a non-Int column or an "
        "out-of-range index")]]
    Result<void> setInt(std::size_t row [[=welder::doc("the row index")]],
                         std::size_t column [[=welder::doc("an Int column's index")]],
                         std::int64_t value [[=welder::doc("the value to store")]],
                         std::size_t element [[=welder::doc("the array element")]] = 0);

    /** Read a float element. */
    [[=welder::doc("Read a float cell."),
      =welder::returns("the value; raises on a non-Float column or an "
        "out-of-range index")]]
    Result<float> getFloat(std::size_t row [[=welder::doc("the row index")]],
                            std::size_t column [[=welder::doc("a Float column's index")]],
                            std::size_t element [[=welder::doc("the array element")]] = 0) const;
    /** Write a float element. */
    [[=welder::doc("Write a float cell."),
      =welder::returns("nothing; raises on a non-Float column or an "
        "out-of-range index")]]
    Result<void> setFloat(std::size_t row [[=welder::doc("the row index")]],
                           std::size_t column [[=welder::doc("a Float column's index")]],
                           float value [[=welder::doc("the value to store")]],
                           std::size_t element [[=welder::doc("the array element")]] = 0);

    /** Read a string element (String columns: the array element; LocString
        columns: the locale slot). The view is valid until the cell mutates.
        @param row     the row index.
        @param column  a String/LocString column's index.
        @param element the array element / locale slot. */
    [[=welder::doc("Read a string cell (String columns: the array element; "
        "LocString columns: the locale slot)."),
      =welder::returns("the text; raises on a non-string column or an "
        "out-of-range index")]]
    Result<std::string_view> getString(std::size_t row [[=welder::doc("the row index")]],
                                        std::size_t column [[=welder::doc("a String/LocString column's index")]],
                                        std::size_t element [[=welder::doc("the array element / locale slot")]] = 0)
    const;
    /** Write a string element. */
    [[=welder::doc("Write a string cell (String columns: the array element; "
        "LocString columns: the locale slot)."),
      =welder::returns("nothing; raises on a non-string column or an "
        "out-of-range index")]]
    Result<void> setString(std::size_t row [[=welder::doc("the row index")]],
                            std::size_t column [[=welder::doc("a String/LocString column's index")]],
                            std::string_view value [[=welder::doc("the text to store")]],
                            std::size_t element [[=welder::doc("the array element / locale slot")]] = 0);

    /** Read a LocString column's flags word. */
    [[=welder::doc("Read a LocString column's flags word."),
      =welder::returns("the flags; raises on a non-LocString column")]]
    Result<std::uint32_t> locstringFlags(std::size_t row [[=welder::doc("the row index")]],
                                          std::size_t column [[=welder::doc("a LocString column's index")]]) const;
    /** Write a LocString column's flags word. */
    [[=welder::doc("Write a LocString column's flags word."),
      =welder::returns("nothing; raises on a non-LocString column")]]
    Result<void> setLocstringFlags(std::size_t row [[=welder::doc("the row index")]],
                                     std::size_t column [[=welder::doc("a LocString column's index")]],
                                     std::uint32_t flags [[=welder::doc("the flags word")]]);

    /** The zero-copy view of numeric/float column @a column.
        @param column the column index.
        @return the view; SchemaMismatch for string-bearing columns. */
    [[=welder::mark::exclude]] // bound by hand: numpy over the raw buffer
    Result<PodColumnView> podColumn(std::size_t column) const;

  private:
    /** Wire the inherited core at this instance's storage. */
    void _wire() {
      _core.wire(&_rows, &_rows, TableInfo{_version, _diskName, _rows.schema()});
    }

    /** Shared bounds/kind gate for the cell accessors. */
    Result<const detail::ColumnRows::Facts*> _checkCell(std::size_t row,
                                                        std::size_t column,
                                                        std::size_t element,
                                                        ColumnType expected) const;

    detail::ColumnRows _rows{}; /**< The column store (sink AND source). */
    ClientVersion _version{}; /**< The serialization target. */
    std::string_view _name; /**< Identifier name (catalog lifetime). */
    std::string_view _diskName; /**< On-disk name (catalog lifetime). */
  };
}
