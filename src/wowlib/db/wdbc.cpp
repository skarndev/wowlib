/** @file
    The WDBC codec (declarations in wdbc.hpp). Non-templated: it walks the
    runtime schema and moves fields through the RecordSink / RecordSource, so the
    whole format lives in one compiled translation unit rather than one per
    generated table. */

#include <wowlib/db/wdbc.hpp>

#include <cstring>
#include <format>
#include <string>
#include <unordered_map>

#include <wowlib/db/codec_detail.hpp>

namespace wowlib::db {
  Result<void> read_wdbc(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink, TableState& state) {
    if (data.size() < sizeof(WdbcHeader))
      return make_error(ErrorCode::TableTruncated,
                        std::format("{}: {} bytes is too small for a WDBC header", info.name, data.size()));
    WdbcHeader header;
    std::memcpy(&header, data.data(), sizeof header);

    const std::size_t stride = detail::record_stride(info.schema);
    if (header.record_size != stride)
      return make_error(ErrorCode::SchemaMismatch,
                        std::format("{}: file record_size {} disagrees with the generated schema stride {}", info.name,
                                    header.record_size, stride));

    const std::size_t expected = sizeof header + std::size_t{header.record_count} * header.record_size + header.
      string_block_size;
    if (data.size() != expected)
      return make_error(ErrorCode::TableTruncated,
                        std::format(
                          "{}: {} bytes on disk, but the header describes {} ({} records of {} bytes "
                          "+ {} string bytes)", info.name, data.size(), expected, header.record_count,
                          header.record_size, header.string_block_size));

    sink.clear();
    state.reset();
    if (auto r = state.strings.read(data.subspan(sizeof header + std::size_t{header.record_count} * header.record_size,
                                                 header.string_block_size)); !r) return r;

    sink.reserve(header.record_count);
    state.string_offsets.reserve(std::size_t{header.record_count} * detail::string_slot_count(info.schema));
    for (std::uint32_t i = 0; i < header.record_count; ++i) {
      const std::size_t rec = sink.add();
      const auto image = data.subspan(sizeof header + std::size_t{i} * stride, stride);
      detail::decode_inline_record(info.schema, rec, image, sink, state);
    }

    state.source_magic = header.magic;
    state.field_count = header.field_count;
    state.record_size = header.record_size;
    return {};
  }

  Result<FileBuffer> write_wdbc(const TableInfo& info, const RecordSource& source, const TableState& state) {
    const std::size_t stride = detail::record_stride(info.schema);
    WdbcHeader header;
    header.record_count = static_cast<std::uint32_t>(source.size());
    header.field_count = state.source_magic != 0 ? state.field_count : detail::field_slot_count(info.schema);
    header.record_size = state.source_magic != 0 ? state.record_size : static_cast<std::uint32_t>(stride);

    detail::StringPool pool{state.strings};
    FileBuffer out;
    out.reserve(sizeof header + source.size() * stride + pool.block.size());
    out.resize(sizeof header);
    std::size_t cursor = 0;
    for (std::size_t r = 0; r < source.size(); ++r)
      detail::encode_inline_record(info.schema, source, r, out, pool, state, cursor);

    header.string_block_size = static_cast<std::uint32_t>(pool.block.size());
    if (auto w = pool.block.write(out); !w) return std::unexpected{w.error()};
    std::memcpy(out.data(), &header, sizeof header);
    return out;
  }
}
