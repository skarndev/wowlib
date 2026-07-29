#pragma once

/** @file
    The WDBC wire header — the client-database format of every pre-Cataclysm
    .dbc (and the .dbc leftovers of Cataclysm..Legion clients): a 20-byte
    header, record_count fixed-stride records, then the string block. Field
    types are not in the file; the schema comes from WoWDBDefs (schema.hpp). */

#include <cstdint>
#include <type_traits>

#include <wowlib/formats/common/fourcc.hpp>

namespace wowlib::db::wire
{
  /** The WDBC magic as memcpy'd off the file front (the bytes "WDBC"). */
  inline constexpr std::uint32_t wdbc_magic =
    formats::four_cc("WDBC", formats::FourCCEndian::forward);

  /** The 20-byte WDBC header (wowdev.wiki/DBC). */
  struct WdbcHeader
  {
    std::uint32_t magic = wdbc_magic;    /**< "WDBC". */
    std::uint32_t record_count = 0;      /**< Records in the record block. */
    std::uint32_t field_count = 0;       /**< Expanded on-disk field slots per record. */
    std::uint32_t record_size = 0;       /**< Record stride in bytes. */
    std::uint32_t string_block_size = 0; /**< String block bytes after the records. */
  };
  static_assert(sizeof(WdbcHeader) == 20 && std::is_trivially_copyable_v<WdbcHeader>);
}
