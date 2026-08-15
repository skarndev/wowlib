#pragma once

/** @file
    Table<Record> — the typed client-database facade over the non-templated
    TableCore engine (table_core.hpp). The record type pins the client version
    and the table identity, so `Table<MapRecord<V>>` IS the Map table of client
    V; everything the table DOES lives in the core, compiled once — this
    template contributes only the typed records vector, the consteval identity,
    and one-line delegations.

    The dbdgen-generated table classes do NOT use this template: they derive
    the welded TableBase chain (so the method surface binds once for all ~4200
    of them) and wire the same core themselves. This facade is the hand-written
    C++ path — a record struct you write yourself gets the full engine by
    naming this one type.

    Round-trip policy (plan of record, 2026-07-29): WDBC and WDB2 are
    byte-perfect (the string block preserves decoded offsets); WDC writes are
    canonical re-encodes with the semantic guarantee (write -> re-read decodes
    to identical values). Encrypted sections always pass through verbatim. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/db/codec.hpp>
#include <wowlib/db/record_bridge.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/db/table_core.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/validation.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::db
{
  /** A client database table: the typed records of one DBFilesClient file.

      A thin typed shell: the records vector and the identity are here, the
      engine is the shared TableCore. Copy/move re-wire the core at the fresh
      records vector — the one obligation a core owner carries.
      @tparam Record the record type (generated, or hand-written per
              schema.hpp's TableRecord contract). */
  template <TableRecord Record>
  class Table
  {
  public:
    /** The client version the record schema belongs to. */
    static constexpr ClientVersion version = Record::version;

    /** The WoWDBDefs table name (e.g. "Map"). */
    static constexpr std::string_view table_name = Record::table_name;

    [[=welder::mark::no_reassign,
      =welder::doc("The decoded records, file order. Mutate in place; write() "
                   "serializes exactly this list.")]]
    std::vector<Record> records;

    Table() { wire(); }
    Table(const Table& o) : records{o.records}, core_{o.core_} { wire(); }
    Table(Table&& o) noexcept : records{std::move(o.records)}, core_{std::move(o.core_)}
    {
      wire();
    }
    Table& operator=(const Table& o)
    {
      records = o.records;
      core_ = o.core_;
      wire();
      return *this;
    }
    Table& operator=(Table&& o) noexcept
    {
      records = std::move(o.records);
      core_ = std::move(o.core_);
      wire();
      return *this;
    }

    /** Decode a table image.
        @param data the whole file content.
        @return nothing, or why the image does not decode. */
    [[=welder::doc("Decode a table file image."),
      =welder::returns("nothing; raises on malformed input or a schema mismatch")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the whole file content")]])
    {
      return core_.read(data);
    }

    /** Load the table from a client filesystem.
        @param fs  the filesystem gateway.
        @param key the file to read.
        @return nothing, or why loading failed. */
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key [[=welder::doc("the file to read")]])
    {
      return core_.read(fs, key);
    }

    /** Serialize the table. A loaded table re-emits the magic it was read from; a
        fresh table uses its client version's canonical .dbc/.db2 format
        (write(fs, key) can override by the target path's extension).
        @param policy how keyless encrypted sections are handled (WDC only).
        @return the file image, or why encoding failed. */
    [[=welder::doc("Serialize the table to a file image; a loaded table re-emits "
                   "the format it was read from. `policy` decides how keyless "
                   "encrypted sections are handled."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write(EncryptedPolicy policy
                             [[=welder::doc("keyless-section handling (WDC only)")]]
                             = EncryptedPolicy::Preserve) const
    {
      return core_.write(policy);
    }

    /** Serialize the table into a client filesystem (project overlay).
        @param fs     the filesystem gateway.
        @param key    the file to write; needs a resolvable path.
        @param policy how keyless encrypted sections are handled (see write()).
        @return nothing, or why saving failed. */
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key [[=welder::doc("the file to write")]],
                       EncryptedPolicy policy
                       [[=welder::doc("keyless-section handling (WDC only)")]]
                       = EncryptedPolicy::Preserve) const
    {
      return core_.write(fs, key, policy);
    }

    /** The preserved string block the record string fields were decoded from.
        @return the decoded (offset, value) entries. */
    [[=welder::getter,
      =welder::doc("The preserved string block the record string fields were decoded "
                   "from; offsets never move, write() appends new strings past its "
                   "end.")]]
    const formats::StringBlock& strings() const { return core_.strings(); }

    /** The encrypted sections skipped on the last read (empty when the file was
        fully decodable or is not a WDC format).
        @return the encrypted-section reports. */
    [[=welder::getter,
      =welder::doc("The encrypted sections skipped on read: their records are not "
                   "in records, but the file re-writes them verbatim.")]]
    const std::vector<EncryptedSection>& encrypted_sections() const
    {
      return core_.encrypted_sections();
    }

    /** Whether every record of the file was decoded (no encrypted sections).
        @return true when records holds the whole table. */
    [[=welder::getter,
      =welder::doc("Whether the whole table decoded — false when encrypted sections "
                   "were skipped.")]]
    bool fully_decoded() const { return core_.fully_decoded(); }

    // There is deliberately NO "value fits its column" check: a column's width
    // IS its member's width (schema.hpp derives one from the other) and the WDC
    // writer sizes each bit-packed field from the actual value range it is
    // given, so nothing reachable through the typed API can overflow what
    // encodes it.
    [[nodiscard]]
    [[=welder::doc(R"(
        Check the logical integrity contracts the records must satisfy to
        survive a write and load in the client: the primary key stays unique,
        and no string holds an embedded NUL the string block would truncate.
        write() never runs this.)"),
      =welder::returns("every violated contract, in record order")]]
    formats::ValidationReport validate() const { return core_.validate(); }

    [[nodiscard]]
    [[=welder::doc("Validate and raise on the first error instead of returning "
                   "a report — the assert-style face of validate()."),
      =welder::returns("nothing; raises when validate() finds any error")]]
    Result<void> ensure_valid() const { return validate().to_result(); }

    /** The erased engine (bindings and tests reach the shared machinery here). */
    const TableCore& core() const { return core_; }

  private:
    /** (Re-)point the core at this instance's vector and identity. */
    void wire()
    {
      static constexpr auto schema = schema_of<Record>();
      core_.wire(&records, &detail::record_ops<Record>,
                 TableInfo{version, table_name, schema});
    }

    TableCore core_;
  };
}
