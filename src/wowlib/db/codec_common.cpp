/** @file
    Shared fixed-stride codec internals (codec_detail.hpp): the byte-perfect
    string pool and the schema-driven inline field decode/encode used by the
    WDBC and WDB2 codecs. */

#include <wowlib/db/codec_detail.hpp>

#include <bit>
#include <cstring>

namespace wowlib::db::detail {
  std::int64_t read_int(std::span<const std::byte> image, std::size_t& pos, std::size_t bytes, bool is_signed) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, image.data() + pos, bytes);
    pos += bytes;
    if (is_signed && bytes < 8) {
      const std::uint64_t sign_bit = std::uint64_t{1} << (bytes * 8 - 1);
      if (raw & sign_bit) raw |= ~((std::uint64_t{1} << (bytes * 8)) - 1);
    }
    return static_cast<std::int64_t>(raw);
  }

  void write_int(FileBuffer& out, std::int64_t value, std::size_t bytes) {
    const auto raw = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < bytes; ++i) out.push_back(static_cast<std::byte>((raw >> (i * 8)) & 0xff));
  }

  namespace {
    /** Read a 4-byte string-block offset at @a pos, advancing it. */
    std::uint32_t read_u32(std::span<const std::byte> image, std::size_t& pos) {
      std::uint32_t v = 0;
      std::memcpy(&v, image.data() + pos, 4);
      pos += 4;
      return v;
    }

    void write_u32(FileBuffer& out, std::uint32_t v) {
      const auto* b = reinterpret_cast<const std::byte*>(&v);
      out.insert(out.end(), b, b + 4);
    }
  }

  StringPool::StringPool(const formats::StringBlock& source) : block{source} {
    if (block.empty()) std::ignore = block.add("");
    // Blizzard blocks always lead with a zero byte
    for (const formats::StringBlock::Entry& entry : block.entries()) lookup.try_emplace(entry.value, entry.offset);
  }

  std::uint32_t StringPool::resolve(std::string_view value,
                                    const std::vector<std::uint32_t>& journal,
                                    std::size_t& cursor) {
    const std::size_t slot = cursor++;
    const std::string owned{value};
    if (slot < journal.size()) {
      const std::uint32_t original = journal[slot];
      if (block.at(original) == owned) return original;
    }
    if (const auto it = lookup.find(owned); it != lookup.end()) return it->second;
    const std::uint32_t offset = block.add(owned);
    lookup.emplace(owned, offset);
    return offset;
  }

  void decode_inline_record(std::span<const Column> schema,
                            std::size_t record,
                            std::span<const std::byte> image,
                            RecordSink& sink,
                            TableState& state) {
    std::size_t pos = 0;
    std::size_t col = 0;
    for (const Column& c : schema) {
      if (c.noninline) {
        ++col;
        continue;
      }
      switch (c.type) {
      case ColumnType::Int:
        for (std::uint16_t e = 0; e < c.array_len; ++e)
          sink.set_int(record, col, e, read_int(image, pos, c.bits / 8u, c.is_signed));
        break;
      case ColumnType::Float:
        for (std::uint16_t e = 0; e < c.array_len; ++e)
          sink.set_float(record, col, e, std::bit_cast<float>(read_u32(image, pos)));
        break;
      case ColumnType::String:
        for (std::uint16_t e = 0; e < c.array_len; ++e) {
          const std::uint32_t off = read_u32(image, pos);
          state.string_offsets.push_back(off);
          sink.set_string(record, col, e, state.strings.at(off));
        }
        break;
      case ColumnType::LocString:
        for (std::uint8_t loc = 0; loc < c.locale_count; ++loc) {
          const std::uint32_t off = read_u32(image, pos);
          state.string_offsets.push_back(off);
          sink.set_string(record, col, loc, state.strings.at(off));
        }
        sink.set_int(record, col, c.locale_count, read_u32(image, pos));
        break;
      }
      ++col;
    }
  }

  void encode_inline_record(std::span<const Column> schema,
                            const RecordSource& source,
                            std::size_t record,
                            FileBuffer& out,
                            StringPool& pool,
                            const TableState& state,
                            std::size_t& cursor) {
    std::size_t col = 0;
    for (const Column& c : schema) {
      if (c.noninline) {
        ++col;
        continue;
      }
      switch (c.type) {
      case ColumnType::Int:
        for (std::uint16_t e = 0; e < c.array_len; ++e) write_int(out, source.get_int(record, col, e), c.bits / 8u);
        break;
      case ColumnType::Float:
        for (std::uint16_t e = 0; e < c.array_len; ++e) write_u32(out, source.get_slot(record, col, e));
        break;
      case ColumnType::String:
        for (std::uint16_t e = 0; e < c.array_len; ++e)
          write_u32(out, pool.resolve(source.get_string(record, col, e), state.string_offsets, cursor));
        break;
      case ColumnType::LocString:
        for (std::uint8_t loc = 0; loc < c.locale_count; ++loc)
          write_u32(out, pool.resolve(source.get_string(record, col, loc), state.string_offsets, cursor));
        write_u32(out, static_cast<std::uint32_t>(source.get_int(record, col, c.locale_count)));
        break;
      }
      ++col;
    }
  }
}
