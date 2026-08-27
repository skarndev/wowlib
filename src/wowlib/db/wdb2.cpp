/** @file
    The WDB2 codec (declarations in wdb2.hpp). Shares the fixed-stride inline
    field decode/encode with WDBC (codec_detail.hpp) and preserves the id-index
    and copy-table blocks verbatim. */

#include <wowlib/db/wdb2.hpp>

#include <cstring>
#include <format>

#include <wowlib/db/codec_detail.hpp>

namespace wowlib::db {
  Result<void> readWdb2(const TableInfo& info, std::span<const std::byte> data, RecordSink& sink, TableState& state) {
    if (data.size() < sizeof(Wdb2Header))
      return makeError(ErrorCode::TableTruncated,
                        std::format("{}: {} bytes is too small for a WDB2 header", info.name, data.size()));
    Wdb2Header header;
    std::memcpy(&header, data.data(), sizeof header);

    const std::size_t stride = detail::recordStride(info.schema);
    if (header.recordSize != stride)
      return makeError(ErrorCode::SchemaMismatch,
                        std::format("{}: file record_size {} disagrees with the generated schema stride {}", info.name,
                                    header.recordSize, stride));
    if (header.maxId != 0 && header.maxId < header.minId)
      return makeError(ErrorCode::TableTruncated,
                        std::format("{}: WDB2 max_id {} below min_id {}", info.name, header.maxId, header.minId));

    const std::size_t indexBytes = header.maxId != 0
                                      ? (std::size_t{header.maxId} - header.minId + 1) * Wdb2IndexEntryBytes
                                      : 0;
    const std::size_t recordsAt = sizeof header + indexBytes;
    const std::size_t expected = recordsAt + std::size_t{header.recordCount} * header.recordSize + header.
      stringBlockSize + header.copyTableSize;
    if (data.size() != expected)
      return makeError(ErrorCode::TableTruncated, std::format(
                          "{}: {} bytes on disk, but the header describes {} ({} records of {} bytes "
                          "+ {} index + {} string + {} copy-table bytes)", info.name, data.size(), expected,
                          header.recordCount, header.recordSize, indexBytes, header.stringBlockSize,
                          header.copyTableSize));

    sink.clear();
    state.reset();
    if (auto r = state.strings.read(data.subspan(recordsAt + std::size_t{header.recordCount} * header.recordSize,
                                                 header.stringBlockSize)); !r) return r;

    sink.reserve(header.recordCount);
    state.stringOffsets.reserve(std::size_t{header.recordCount} * detail::stringSlotCount(info.schema));
    for (std::uint32_t i = 0; i < header.recordCount; ++i) {
      const std::size_t rec = sink.add();
      const auto image = data.subspan(recordsAt + std::size_t{i} * stride, stride);
      detail::decodeInlineRecord(info.schema, rec, image, sink, state);
    }

    const auto indexBlock = data.subspan(sizeof header, indexBytes);
    state.wdb2Index.assign(indexBlock.begin(), indexBlock.end());
    const auto copyBlock = data.subspan(expected - header.copyTableSize, header.copyTableSize);
    state.wdb2Copy.assign(copyBlock.begin(), copyBlock.end());
    const auto* hb = reinterpret_cast<const std::byte*>(&header);
    state.wdb2Header.assign(hb, hb + sizeof header);
    state.sourceMagic = header.magic;
    state.fieldCount = header.fieldCount;
    state.recordSize = header.recordSize;
    return {};
  }

  Result<FileBuffer> writeWdb2(const TableInfo& info, const RecordSource& source, const TableState& state) {
    const std::size_t stride = detail::recordStride(info.schema);
    const bool loaded = state.sourceMagic == Wdb2Magic;

    Wdb2Header header{};
    if (loaded && state.wdb2Header.size() == sizeof header) std::memcpy(
      &header, state.wdb2Header.data(), sizeof header);

    if (loaded && !state.wdb2Index.empty() && source.size() != header.recordCount)
      return makeError(ErrorCode::InvalidEntityState,
                        std::format(
                          "{}: the WDB2 id-index block cannot be rebuilt yet; adding or removing "
                          "records of an indexed table is unsupported (had {}, have {})", info.name,
                          header.recordCount, source.size()));

    header.magic = Wdb2Magic;
    header.recordCount = static_cast<std::uint32_t>(source.size());
    header.fieldCount = loaded ? state.fieldCount : detail::fieldSlotCount(info.schema);
    header.recordSize = loaded ? state.recordSize : static_cast<std::uint32_t>(stride);
    if (!loaded) header.build = info.version.build;

    detail::StringPool pool{state.strings};
    FileBuffer out;
    out.reserve(
      sizeof header + state.wdb2Index.size() + source.size() * stride + pool.block.size() + state.wdb2Copy.size());
    out.resize(sizeof header);
    out.insert(out.end(), state.wdb2Index.begin(), state.wdb2Index.end());
    std::size_t cursor = 0;
    for (std::size_t r = 0; r < source.size(); ++r)
      detail::encodeInlineRecord(info.schema, source, r, out, pool, state, cursor);

    header.stringBlockSize = static_cast<std::uint32_t>(pool.block.size());
    if (auto w = pool.block.write(out); !w) return std::unexpected{w.error()};
    out.insert(out.end(), state.wdb2Copy.begin(), state.wdb2Copy.end());
    header.copyTableSize = static_cast<std::uint32_t>(state.wdb2Copy.size());
    std::memcpy(out.data(), &header, sizeof header);
    return out;
  }
}
