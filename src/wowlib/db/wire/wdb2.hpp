#pragma once

/** @file
    The WDB2 wire header — the .db2 format of the Cataclysm..WoD MPQ/early-CASC
    era (4.0.1.12911 .. 7.0.1.20740): the WDBC layout plus table identity
    (table_hash/build/timestamp), an optional id-index block ahead of the
    records (engaged when max_id != 0) and an optional trailing copy table.
    Field types are still not in the file; the schema comes from WoWDBDefs.

    UNVERIFIED against real clients: no Cata/MoP/WoD install is available
    locally — the layout follows wowdev.wiki/DB2 and is exercised by synthetic
    unit tests only (plan of record, stage 2). */

#include <cstdint>
#include <type_traits>

#include <wowlib/formats/common/fourcc.hpp>

namespace wowlib::db::wire
{
  /** The WDB2 magic as memcpy'd off the file front (the bytes "WDB2"). */
  inline constexpr std::uint32_t wdb2_magic =
    formats::four_cc("WDB2", formats::FourCCEndian::forward);

  /** The 48-byte WDB2 header (wowdev.wiki/DB2). */
  struct Wdb2Header
  {
    std::uint32_t magic = wdb2_magic;    /**< "WDB2". */
    std::uint32_t record_count = 0;      /**< Records in the record block. */
    std::uint32_t field_count = 0;       /**< Expanded on-disk field slots per record. */
    std::uint32_t record_size = 0;       /**< Record stride in bytes. */
    std::uint32_t string_block_size = 0; /**< String block bytes after the records. */
    std::uint32_t table_hash = 0;        /**< SStrHash of the uppercased table name. */
    std::uint32_t build = 0;             /**< Client build the file was generated for. */
    std::uint32_t timestamp_last_written = 0; /**< Unix timestamp, often 0. */
    std::uint32_t min_id = 0;            /**< Lowest record id; 0 when no index block. */
    std::uint32_t max_id = 0;            /**< Highest record id; non-zero engages the index block. */
    std::uint32_t locale = 0;            /**< Locale mask of the generating client. */
    std::uint32_t copy_table_size = 0;   /**< Trailing copy-table bytes (id pairs). */
  };
  static_assert(sizeof(Wdb2Header) == 48 && std::is_trivially_copyable_v<Wdb2Header>);

  /** The per-id index block entry stride when max_id != 0:
      `int32 indices[max_id - min_id + 1]` followed by
      `int16 string_lengths[max_id - min_id + 1]` — 6 bytes per id in total. */
  inline constexpr std::size_t wdb2_index_entry_bytes = 6;
}
