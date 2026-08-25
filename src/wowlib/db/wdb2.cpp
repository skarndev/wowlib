/** @file
    The WDB2 codec (declarations in wdb2.hpp). Shares the fixed-stride inline
    field decode/encode with WDBC (codec_detail.hpp) and preserves the id-index
    and copy-table blocks verbatim. */

#include <wowlib/db/wdb2.hpp>

#include <cstring>
#include <format>

#include <wowlib/db/codec_detail.hpp>

namespace wowlib::db {
  Result<void> read_wdb2(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink, TableState& state) {
    if (data.size() < sizeof(Wdb2Header))
      return make_error(ErrorCode::TableTruncated,
                        std::format("{}: {} bytes is too small for a WDB2 header", info.name, data.size()));
    Wdb2Header header;
    std::memcpy(&header, data.data(), sizeof header);

    const std::size_t stride = detail::record_stride(info.schema);
    if (header.record_size != stride)
      return make_error(ErrorCode::SchemaMismatch,
                        std::format("{}: file record_size {} disagrees with the generated schema stride {}", info.name,
                                    header.record_size, stride));
    if (header.max_id != 0 && header.max_id < header.min_id)
      return make_error(ErrorCode::TableTruncated,
                        std::format("{}: WDB2 max_id {} below min_id {}", info.name, header.max_id, header.min_id));

    const std::size_t index_bytes = header.max_id != 0
                                      ? (std::size_t{header.max_id} - header.min_id + 1) * wdb2_index_entry_bytes
                                      : 0;
    const std::size_t records_at = sizeof header + index_bytes;
    const std::size_t expected = records_at + std::size_t{header.record_count} * header.record_size + header.
      string_block_size + header.copy_table_size;
    if (data.size() != expected)
      return make_error(ErrorCode::TableTruncated, std::format(
                          "{}: {} bytes on disk, but the header describes {} ({} records of {} bytes "
                          "+ {} index + {} string + {} copy-table bytes)", info.name, data.size(), expected,
                          header.record_count, header.record_size, index_bytes, header.string_block_size,
                          header.copy_table_size));

    sink.clear();
    state.reset();
    if (auto r = state.strings.read(data.subspan(records_at + std::size_t{header.record_count} * header.record_size,
                                                 header.string_block_size)); !r) return r;

    sink.reserve(header.record_count);
    state.string_offsets.reserve(std::size_t{header.record_count} * detail::string_slot_count(info.schema));
    for (std::uint32_t i = 0; i < header.record_count; ++i) {
      const std::size_t rec = sink.add();
      const auto image = data.subspan(records_at + std::size_t{i} * stride, stride);
      detail::decode_inline_record(info.schema, rec, image, sink, state);
    }

    const auto index_block = data.subspan(sizeof header, index_bytes);
    state.wdb2_index.assign(index_block.begin(), index_block.end());
    const auto copy_block = data.subspan(expected - header.copy_table_size, header.copy_table_size);
    state.wdb2_copy.assign(copy_block.begin(), copy_block.end());
    const auto* hb = reinterpret_cast<const std::byte*>(&header);
    state.wdb2_header.assign(hb, hb + sizeof header);
    state.source_magic = header.magic;
    state.field_count = header.field_count;
    state.record_size = header.record_size;
    return {};
  }

  Result<FileBuffer> write_wdb2(const TableInfo& info, const RecordSource& source, const TableState& state) {
    const std::size_t stride = detail::record_stride(info.schema);
    const bool loaded = state.source_magic == wdb2_magic;

    Wdb2Header header{};
    if (loaded && state.wdb2_header.size() == sizeof header) std::memcpy(
      &header, state.wdb2_header.data(), sizeof header);

    if (loaded && !state.wdb2_index.empty() && source.size() != header.record_count)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format(
                          "{}: the WDB2 id-index block cannot be rebuilt yet; adding or removing "
                          "records of an indexed table is unsupported (had {}, have {})", info.name,
                          header.record_count, source.size()));

    header.magic = wdb2_magic;
    header.record_count = static_cast<std::uint32_t>(source.size());
    header.field_count = loaded ? state.field_count : detail::field_slot_count(info.schema);
    header.record_size = loaded ? state.record_size : static_cast<std::uint32_t>(stride);
    if (!loaded) header.build = info.version.build;

    detail::StringPool pool{state.strings};
    FileBuffer out;
    out.reserve(
      sizeof header + state.wdb2_index.size() + source.size() * stride + pool.block.size() + state.wdb2_copy.size());
    out.resize(sizeof header);
    out.insert(out.end(), state.wdb2_index.begin(), state.wdb2_index.end());
    std::size_t cursor = 0;
    for (std::size_t r = 0; r < source.size(); ++r)
      detail::encode_inline_record(info.schema, source, r, out, pool, state, cursor);

    header.string_block_size = static_cast<std::uint32_t>(pool.block.size());
    if (auto w = pool.block.write(out); !w) return std::unexpected{w.error()};
    out.insert(out.end(), state.wdb2_copy.begin(), state.wdb2_copy.end());
    header.copy_table_size = static_cast<std::uint32_t>(state.wdb2_copy.size());
    std::memcpy(out.data(), &header, sizeof header);
    return out;
  }
}
