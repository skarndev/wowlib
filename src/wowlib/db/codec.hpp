#pragma once

/** @file
    The type-erased boundary between the templated Table<Record> facade
    (table.hpp) and the per-format codecs (wdbc.{hpp,cpp}, wdb2.{hpp,cpp},
    and the WDC family under wdc/).

    The codecs are NOT templated on the record type: they drive the record
    vector only through the abstract RecordSink (decode target) / RecordSource
    (encode source), reading and writing one field at a time by (column,
    element) against the runtime schema (schema.hpp). record_bridge.hpp supplies
    the one small per-record adapter that implements these; everything heavy —
    headers, string blocks, WDC3 sections, compression, pallet/common blocks,
    copy tables, bit packing — is compiled ONCE in the codec .cpp files instead
    of once per generated table. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/formats/common/string_block.hpp>

namespace wowlib::db
{
  /** How write() treats a table that still holds keyless (undecryptable)
      encrypted sections. */
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        How a client-database write handles keyless encrypted sections: Preserve
        re-emits the file's original bytes verbatim (encrypted content intact,
        edits to decoded rows NOT applied); Drop writes only the decoded rows as
        a plain unencrypted table, discarding the rows behind missing keys.)")
  ]] EncryptedPolicy
  {
    Preserve,  /**< Re-emit the original image verbatim; edits are not applied. */
    Drop       /**< Write only decoded rows as plaintext; keyless rows are dropped. */
  };

  /** One WDC section whose records could not be decoded because they are
      encrypted under a TACT key wowlib does not hold. Record-independent, so it
      lives at namespace scope (welded once, shared by every Table). */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An encrypted WDC section: its records are behind a TACT key "
                 "wowlib does not hold, so they are absent from records.")
  ]] EncryptedSection
  {
    [[=welder::doc("The section's TACT key lookup hash (names the missing key).")]]
    std::uint64_t key_hash = 0;

    [[=welder::doc("How many records the encrypted section holds.")]]
    std::uint32_t record_count = 0;

    [[=welder::doc("The ids of the encrypted records, when the file lists them.")]]
    std::vector<std::uint32_t> ids;
  };

  /** The per-table identity a codec needs beyond the record data: the client
      version (format selection, diagnostics), the WoWDBDefs table name
      (diagnostics), and the runtime schema. Every other derived quantity —
      record stride, field-slot count, inline column count, the id column's
      inline index / non-inline flag — the codecs compute from @ref schema. */
  struct TableInfo
  {
    ClientVersion version{};            /**< The record's client version. */
    std::string_view name;             /**< The WoWDBDefs table name. */
    std::span<const Column> schema;    /**< The record schema (schema_of<Record>()). */
  };

  /** The preserved decode state a table carries between read() and write() so a
      WDBC/WDB2 write is byte-perfect and a WDC write reproduces the original
      compression. Non-templated: owned by the facade, filled and read by the
      codecs. */
  struct TableState
  {
    std::uint32_t source_magic = 0;  /**< The magic read() sniffed; 0 for a fresh table. */
    std::uint32_t field_count = 0;   /**< Preserved header field_count (not always derivable). */
    std::uint32_t record_size = 0;   /**< Preserved header record_size. */
    formats::StringBlock strings;    /**< Preserved string block; offsets never move. */

    /** The original string-block offset of every string field, row-major in
        record then schema order — the byte-perfect write-back journal. */
    std::vector<std::uint32_t> string_offsets;

    std::vector<std::byte> wdb2_header;  /**< Preserved WDB2 header identity bytes. */
    std::vector<std::byte> wdb2_index;   /**< Preserved WDB2 id-index block, verbatim. */
    std::vector<std::byte> wdb2_copy;    /**< Preserved WDB2 copy table, verbatim. */

    std::vector<EncryptedSection> encrypted; /**< Encrypted WDC sections skipped on read. */
    std::uint32_t wdc_table_hash = 0;    /**< Preserved WDC3 header table_hash. */
    std::uint32_t wdc_layout_hash = 0;   /**< Preserved WDC3 header layout_hash. */
    std::uint32_t wdc_locale = 0;        /**< Preserved WDC3 header locale. */
    std::vector<std::byte> wdc_original; /**< Raw image kept when the file has encrypted sections. */
    std::vector<std::uint8_t> wdc_kinds; /**< Original per-inline-column compression (WdcCompression). */
    std::vector<std::byte> wdc5_prefix;  /**< Preserved WDC5 {version, schema string} header prefix. */

    /** Reset the per-read state before decoding a fresh image. */
    void reset()
    {
      strings = {};
      string_offsets.clear();
      wdb2_header.clear();
      wdb2_index.clear();
      wdb2_copy.clear();
      encrypted.clear();
      wdc_original.clear();
      wdc_kinds.clear();
      wdc5_prefix.clear();
    }
  };

  /** The decode target: the codecs build the record vector through this, one
      field at a time. `add()` appends a default record and returns its index;
      `set_*` scatter a decoded value into (record, column, element) — element
      indexes an array member, a LocString language slot (its flags being the
      element at index locale_count), or 0 for a scalar. record_bridge.hpp's
      TypedRecordSink<Record> implements it over a std::vector<Record>&. */
  class RecordSink
  {
  public:
    virtual ~RecordSink() = default;
    virtual void clear() = 0;
    virtual void reserve(std::size_t n) = 0;
    virtual std::size_t add() = 0;
    virtual std::size_t size() const = 0;
    virtual std::uint32_t id_of(std::size_t record) const = 0;
    virtual void set_int(std::size_t record, std::size_t column, std::size_t element,
                         std::int64_t value) = 0;
    virtual void set_float(std::size_t record, std::size_t column, std::size_t element,
                           float value) = 0;
    virtual void set_string(std::size_t record, std::size_t column, std::size_t element,
                            std::string_view value) = 0;
    /** Clone record @a src into a fresh record and set its id column to @a new_id
        (WDC copy-table materialization). */
    virtual void clone_with_id(std::size_t src, std::uint32_t new_id) = 0;
    /** The first record whose id equals @a id, or size() when none (copy source
        lookup). */
    virtual std::size_t find_by_id(std::uint32_t id) const = 0;
  };

  /** The encode source: the codecs read the record vector through this. `get_int`
      returns a signed-widened integer (range scan, bit packing); `get_slot`
      returns the 32-bit pallet/common/None slot (an integer truncated to 32
      bits, or a float's bit pattern); `get_string` the string value. The element
      index follows the RecordSink convention. TypedRecordSource<Record>
      implements it over a const std::vector<Record>&. */
  class RecordSource
  {
  public:
    virtual ~RecordSource() = default;
    virtual std::size_t size() const = 0;
    virtual std::uint32_t id_of(std::size_t record) const = 0;
    virtual std::int64_t get_int(std::size_t record, std::size_t column,
                                 std::size_t element) const = 0;
    virtual std::uint32_t get_slot(std::size_t record, std::size_t column,
                                   std::size_t element) const = 0;
    virtual std::string_view get_string(std::size_t record, std::size_t column,
                                        std::size_t element) const = 0;
  };

  namespace detail
  {
    /** The fixed record stride (WDBC/WDB2) of @a schema. */
    inline std::size_t record_stride(std::span<const Column> schema)
    {
      std::size_t bytes = 0;
      for (const Column& col : schema)
        bytes += col.inline_bytes();
      return bytes;
    }

    /** The expanded on-disk field-slot count of @a schema (WDBC/WDB2 field_count). */
    inline std::uint32_t field_slot_count(std::span<const Column> schema)
    {
      std::uint32_t slots = 0;
      for (const Column& col : schema)
        slots += col.field_slots();
      return slots;
    }

    /** The per-record string-reference count of @a schema (the offset journal width). */
    inline std::size_t string_slot_count(std::span<const Column> schema)
    {
      std::size_t slots = 0;
      for (const Column& col : schema)
        slots += col.string_slots();
      return slots;
    }

    /** The number of INLINE columns (everything not $noninline$). */
    inline std::size_t inline_column_count(std::span<const Column> schema)
    {
      std::size_t n = 0;
      for (const Column& col : schema)
        n += col.noninline ? 0 : 1;
      return n;
    }

    /** Whether the id column is stored outside the record ($noninline$ id). */
    inline bool id_is_noninline(std::span<const Column> schema)
    {
      for (const Column& col : schema)
        if (col.is_id)
          return col.noninline;
      return false;
    }

    /** The inline field index of an inline id column (0 when non-inline). */
    inline std::uint16_t id_field_index(std::span<const Column> schema)
    {
      std::uint16_t idx = 0;
      for (const Column& col : schema)
      {
        if (col.noninline)
          continue;
        if (col.is_id)
          return idx;
        ++idx;
      }
      return 0;
    }
  }
}
