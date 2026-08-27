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

namespace wowlib::db {
  /** How write() treats a table that still holds keyless (undecryptable)
      encrypted sections. */
  enum class [[
      =welder::weld,
      =welder::doc(R"(
        How a client-database write handles keyless encrypted sections: Preserve
        re-emits the file's original bytes verbatim (encrypted content intact,
        edits to decoded rows NOT applied); Drop writes only the decoded rows as
        a plain unencrypted table, discarding the rows behind missing keys.)")
    ]] EncryptedPolicy {
    Preserve, /**< Re-emit the original image verbatim; edits are not applied. */
    Drop /**< Write only decoded rows as plaintext; keyless rows are dropped. */
  };

  /** One WDC section whose records could not be decoded because they are
      encrypted under a TACT key wowlib does not hold. Record-independent, so it
      lives at namespace scope (welded once, shared by every Table). */
  struct [[
      =welder::weld,
      =welder::doc(
        "An encrypted WDC section: its records are behind a TACT key "
        "wowlib does not hold, so they are absent from records.")
    ]] EncryptedSection {
    [[=welder::doc(
      "The section's TACT key lookup hash (names the missing key).")]]
    std::uint64_t keyHash = 0;

    [[=welder::doc("How many records the encrypted section holds.")]]
    std::uint32_t recordCount = 0;

    [[=welder::doc(
      "The ids of the encrypted records, when the file lists them.")]]
    std::vector<std::uint32_t> ids;
  };

  /** The per-table identity a codec needs beyond the record data: the client
      version (format selection, diagnostics), the WoWDBDefs table name
      (diagnostics), and the runtime schema. Every other derived quantity —
      record stride, field-slot count, inline column count, the id column's
      inline index / non-inline flag — the codecs compute from @ref schema. */
  struct TableInfo {
    ClientVersion version{}; /**< The record's client version. */
    std::string_view name; /**< The WoWDBDefs table name. */
    std::span<const Column> schema; /**< The record schema (schemaOf<Record>()). */
  };

  /** The preserved decode state a table carries between read() and write() so a
      WDBC/WDB2 write is byte-perfect and a WDC write reproduces the original
      compression. Non-templated: owned by the facade, filled and read by the
      codecs. */
  struct TableState {
    std::uint32_t sourceMagic = 0; /**< The magic read() sniffed; 0 for a fresh table. */
    std::uint32_t fieldCount = 0; /**< Preserved header fieldCount (not always derivable). */
    std::uint32_t recordSize = 0; /**< Preserved header recordSize. */
    formats::StringBlock strings; /**< Preserved string block; offsets never move. */

    /** The original string-block offset of every string field, row-major in
        record then schema order — the byte-perfect write-back journal. */
    std::vector<std::uint32_t> stringOffsets;

    std::vector<std::byte> wdb2Header; /**< Preserved WDB2 header identity bytes. */
    std::vector<std::byte> wdb2Index; /**< Preserved WDB2 id-index block, verbatim. */
    std::vector<std::byte> wdb2Copy; /**< Preserved WDB2 copy table, verbatim. */

    std::vector<EncryptedSection> encrypted; /**< Encrypted WDC sections skipped on read. */
    std::uint32_t wdcTableHash = 0; /**< Preserved WDC3 header tableHash. */
    std::uint32_t wdcLayoutHash = 0; /**< Preserved WDC3 header layoutHash. */
    std::uint32_t wdcLocale = 0; /**< Preserved WDC3 header locale. */
    std::vector<std::byte> wdcOriginal; /**< Raw image kept when the file has encrypted sections. */
    std::vector<std::uint8_t> wdcKinds; /**< Original per-inline-column compression (WdcCompression). */
    std::vector<std::byte> wdc5Prefix; /**< Preserved WDC5 {version, schema string} header prefix. */

    /** Reset the per-read state before decoding a fresh image. */
    void reset() {
      strings = {};
      stringOffsets.clear();
      wdb2Header.clear();
      wdb2Index.clear();
      wdb2Copy.clear();
      encrypted.clear();
      wdcOriginal.clear();
      wdcKinds.clear();
      wdc5Prefix.clear();
    }
  };

  /** The decode target: the codecs build the record vector through this, one
      field at a time. `add()` appends a default record and returns its index;
      `set_*` scatter a decoded value into (record, column, element) — element
      indexes an array member, a LocString language slot (its flags being the
      element at index localeCount), or 0 for a scalar. record_bridge.hpp's
      ErasedRecordSink implements it over a std::vector<Record>& (record_bridge.hpp). */
  class RecordSink {
  public:
    virtual ~RecordSink() = default;
    virtual void clear() = 0;
    virtual void reserve(std::size_t n) = 0;
    virtual std::size_t add() = 0;
    virtual std::size_t size() const = 0;
    virtual std::uint32_t idOf(std::size_t record) const = 0;
    virtual void setInt(std::size_t record, std::size_t column, std::size_t element, std::int64_t value) = 0;
    virtual void setFloat(std::size_t record, std::size_t column, std::size_t element, float value) = 0;
    virtual void setString(std::size_t record, std::size_t column, std::size_t element, std::string_view value) = 0;
    /** Clone record @a src into a fresh record and set its id column to @a newId
        (WDC copy-table materialization). */
    virtual void cloneWithId(std::size_t src, std::uint32_t newId) = 0;
    /** The first record whose id equals @a id, or size() when none (copy source
        lookup). */
    virtual std::size_t findById(std::uint32_t id) const = 0;
  };

  /** The encode source: the codecs read the record vector through this. `getInt`
      returns a signed-widened integer (range scan, bit packing); `getSlot`
      returns the 32-bit pallet/common/None slot (an integer truncated to 32
      bits, or a float's bit pattern); `getString` the string value. The element
      index follows the RecordSink convention. ErasedRecordSource
      implements it over a const std::vector<Record>&. */
  class RecordSource {
  public:
    virtual ~RecordSource() = default;
    virtual std::size_t size() const = 0;
    virtual std::uint32_t idOf(std::size_t record) const = 0;
    virtual std::int64_t getInt(std::size_t record, std::size_t column, std::size_t element) const = 0;
    virtual std::uint32_t getSlot(std::size_t record, std::size_t column, std::size_t element) const = 0;
    virtual std::string_view getString(std::size_t record, std::size_t column, std::size_t element) const = 0;
  };

  namespace detail {
    /** The fixed record stride (WDBC/WDB2) of @a schema. */
    inline std::size_t recordStride(std::span<const Column> schema) {
      std::size_t bytes = 0;
      for (const Column& col : schema) bytes += col.inlineBytes();
      return bytes;
    }

    /** The expanded on-disk field-slot count of @a schema (WDBC/WDB2 fieldCount). */
    inline std::uint32_t fieldSlotCount(std::span<const Column> schema) {
      std::uint32_t slots = 0;
      for (const Column& col : schema) slots += col.fieldSlots();
      return slots;
    }

    /** The per-record string-reference count of @a schema (the offset journal width). */
    inline std::size_t stringSlotCount(std::span<const Column> schema) {
      std::size_t slots = 0;
      for (const Column& col : schema) slots += col.stringSlots();
      return slots;
    }

    /** The number of INLINE columns (everything not $noninline$). */
    inline std::size_t inlineColumnCount(std::span<const Column> schema) {
      std::size_t n = 0;
      for (const Column& col : schema) n += col.noninline ? 0 : 1;
      return n;
    }

    /** Whether the id column is stored outside the record ($noninline$ id). */
    inline bool idIsNoninline(std::span<const Column> schema) {
      for (const Column& col : schema)
        if (col.isId) return col.noninline;
      return false;
    }

    /** The inline field index of an inline id column (0 when non-inline). */
    inline std::uint16_t idFieldIndex(std::span<const Column> schema) {
      std::uint16_t idx = 0;
      for (const Column& col : schema) {
        if (col.noninline) continue;
        if (col.isId) return idx;
        ++idx;
      }
      return 0;
    }
  }
}
