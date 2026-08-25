#pragma once

/** @file
    WDBC — the client-database format of every pre-Cataclysm .dbc (and the .dbc
    leftovers of Cataclysm..Legion clients): a 20-byte header, record_count
    fixed-stride records, then the string block. Field types are not in the file;
    the schema comes from WoWDBDefs (schema.hpp).

    This header carries the binary header struct and the NON-templated codec
    entry points; the implementation (wdbc.cpp) drives records through the
    RecordSink / RecordSource so it is compiled once, not per generated table. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/db/codec.hpp>
#include <wowlib/formats/common/fourcc.hpp>

namespace wowlib::db {
  /** The WDBC magic as memcpy'd off the file front (the bytes "WDBC"). */
  inline constexpr std::uint32_t wdbc_magic = formats::four_cc("WDBC", formats::FourCCEndian::forward);

  /** The 20-byte WDBC header (wowdev.wiki/DBC). */
  struct WdbcHeader {
    std::uint32_t magic = wdbc_magic; /**< "WDBC". */
    std::uint32_t record_count = 0; /**< Records in the record block. */
    std::uint32_t field_count = 0; /**< Expanded on-disk field slots per record. */
    std::uint32_t record_size = 0; /**< Record stride in bytes. */
    std::uint32_t string_block_size = 0; /**< String block bytes after the records. */
  };

  static_assert(sizeof(WdbcHeader) == 20 && std::is_trivially_copyable_v<WdbcHeader>);

  /** Decode a WDBC image: 20-byte header, fixed-stride records, string block.
      Records are appended to @a sink; @a state keeps the string block and the
      per-field original-offset journal for a byte-perfect write-back.
      @param info  the table identity + schema.
      @param data  the whole file content.
      @param sink  the decode target (records appended in file order).
      @param state the preserved-state store (reset and filled here).
      @return nothing, or why the image does not decode. */
  Result<void> read_wdbc(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink, TableState& state);

  /** Encode a WDBC image; the string block preserves decoded offsets and appends
      new/changed strings (byte-perfect for an unmodified table).
      @param info   the table identity + schema.
      @param source the records to encode.
      @param state  the preserved-state store (read only).
      @return the file bytes, or why encoding failed. */
  Result<FileBuffer> write_wdbc(const TableInfo& info, const RecordSource& source, const TableState& state);
}
