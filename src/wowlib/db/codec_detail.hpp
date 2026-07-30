#pragma once

/** @file
    Shared codec internals for the fixed-stride formats (WDBC, WDB2): the string
    pool that reproduces byte-perfect offsets, and the per-record inline
    field decode/encode driven by the runtime schema. All non-templated —
    compiled once in codec_common.cpp. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <wowlib/core/buffer.hpp>
#include <wowlib/db/codec.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/formats/common/string_block.hpp>

namespace wowlib::db::detail
{
  /** Read a little-endian integer of @a bytes bytes at @a pos, sign-extended
      when @a is_signed, advancing @a pos. */
  std::int64_t read_int(std::span<const std::byte> image, std::size_t& pos, std::size_t bytes,
                        bool is_signed);

  /** Append @a value's low @a bytes bytes (little-endian) to @a out. */
  void write_int(FileBuffer& out, std::int64_t value, std::size_t bytes);

  /** A writable string block resolving values to byte-perfect offsets: the
      journaled original offset while the value still matches, else an equal
      existing entry, else a fresh append. */
  struct StringPool
  {
    formats::StringBlock block;
    std::unordered_map<std::string, std::uint32_t> lookup;

    /** Copy @a source (leading with the mandatory zero byte) and index it. */
    explicit StringPool(const formats::StringBlock& source);

    /** The offset to store for @a value: @a journal[cursor] when it still holds
        @a value, else a deduped/appended offset. @a cursor advances one slot. */
    std::uint32_t resolve(std::string_view value, const std::vector<std::uint32_t>& journal,
                          std::size_t& cursor);
  };

  /** Decode one fixed-stride record's inline fields into @a sink and journal its
      string offsets (record then schema then element order). */
  void decode_inline_record(std::span<const Column> schema, std::size_t record,
                            std::span<const std::byte> image, RecordSink& sink,
                            TableState& state);

  /** Encode one fixed-stride record's inline fields from @a source, appending to
      @a out and resolving strings through @a pool (@a cursor tracks the journal). */
  void encode_inline_record(std::span<const Column> schema, const RecordSource& source,
                            std::size_t record, FileBuffer& out, StringPool& pool,
                            const TableState& state, std::size_t& cursor);
}
