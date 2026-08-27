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
  Result<void> readWdbc(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink, TableState& state) {
    if (data.size() < sizeof(WdbcHeader))
      return makeError(ErrorCode::TableTruncated,
                        std::format("{}: {} bytes is too small for a WDBC header", info.name, data.size()));
    WdbcHeader header;
    std::memcpy(&header, data.data(), sizeof header);

    const std::size_t stride = detail::recordStride(info.schema);
    if (header.recordSize != stride)
      return makeError(ErrorCode::SchemaMismatch,
                        std::format("{}: file record_size {} disagrees with the generated schema stride {}", info.name,
                                    header.recordSize, stride));

    const std::size_t expected = sizeof header + std::size_t{header.recordCount} * header.recordSize + header.
      stringBlockSize;
    if (data.size() != expected)
      return makeError(ErrorCode::TableTruncated,
                        std::format(
                          "{}: {} bytes on disk, but the header describes {} ({} records of {} bytes "
                          "+ {} string bytes)", info.name, data.size(), expected, header.recordCount,
                          header.recordSize, header.stringBlockSize));

    sink.clear();
    state.reset();
    if (auto r = state.strings.read(data.subspan(sizeof header + std::size_t{header.recordCount} * header.recordSize,
                                                 header.stringBlockSize)); !r) return r;

    sink.reserve(header.recordCount);
    state.stringOffsets.reserve(std::size_t{header.recordCount} * detail::stringSlotCount(info.schema));
    for (std::uint32_t i = 0; i < header.recordCount; ++i) {
      const std::size_t rec = sink.add();
      const auto image = data.subspan(sizeof header + std::size_t{i} * stride, stride);
      detail::decodeInlineRecord(info.schema, rec, image, sink, state);
    }

    state.sourceMagic = header.magic;
    state.fieldCount = header.fieldCount;
    state.recordSize = header.recordSize;
    return {};
  }

  Result<FileBuffer> writeWdbc(const TableInfo& info, const RecordSource& source, const TableState& state) {
    const std::size_t stride = detail::recordStride(info.schema);
    WdbcHeader header;
    header.recordCount = static_cast<std::uint32_t>(source.size());
    header.fieldCount = state.sourceMagic != 0 ? state.fieldCount : detail::fieldSlotCount(info.schema);
    header.recordSize = state.sourceMagic != 0 ? state.recordSize : static_cast<std::uint32_t>(stride);

    detail::StringPool pool{state.strings};
    FileBuffer out;
    out.reserve(sizeof header + source.size() * stride + pool.block.size());
    out.resize(sizeof header);
    std::size_t cursor = 0;
    for (std::size_t r = 0; r < source.size(); ++r)
      detail::encodeInlineRecord(info.schema, source, r, out, pool, state, cursor);

    header.stringBlockSize = static_cast<std::uint32_t>(pool.block.size());
    if (auto w = pool.block.write(out); !w) return std::unexpected{w.error()};
    std::memcpy(out.data(), &header, sizeof header);
    return out;
  }
}
