#pragma once

/** @file
    Table<Record> — the client-database entity: a typed record vector decoded
    from / encoded to the on-disk table formats. Speaks WDBC (every
    pre-Cataclysm .dbc) and WDB2 (the Cata..WoD .db2); the WDC formats join as
    further protected codec members dispatched off the sniffed magic.

    Round-trip policy (plan of record, 2026-07-29): WDBC and WDB2 are
    byte-perfect — the string block is preserved as decoded entries whose
    offsets never move, and every record string field remembers the offset it
    was read from, reusing it verbatim while the value still matches. New or
    changed strings dedup against the block, then append. WDB2's id-index and
    copy-table blocks are preserved verbatim (the format is unverified — no
    Cata..WoD client is installed locally). */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/db/wire/wdb2.hpp>
#include <wowlib/db/wire/wdbc.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::db
{
  /** A client database table: the typed records of one DBFilesClient file.

      The record type is generated from WoWDBDefs by dbdgen (a flat struct whose
      member types carry the column shapes — schema.hpp) and pins both the
      client version and the table identity, so `Table<MapRecord<V>>` IS the
      Map table of client V.
      @tparam Record the generated record type. */
  template <TableRecord Record>
  class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A client database table (DBFilesClient): the typed records of one
        .dbc/.db2 file, decoded against the WoWDBDefs schema its record class
        was generated from. Reading preserves the string block and the exact
        header values, so an unmodified table writes back byte-identically.)")
  ]] Table
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

    /** Decode a table image.
        @param data the whole file content.
        @return nothing, or why the image does not decode. */
    [[=welder::doc("Decode a table file image."),
      =welder::returns("nothing; raises on malformed input or a schema mismatch")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the whole file content")]])
    {
      if (data.size() < sizeof(std::uint32_t))
        return make_error(ErrorCode::TableTruncated,
                          std::format("{}: {} bytes is too small for a client database",
                                      table_name, data.size()));
      std::uint32_t magic = 0;
      std::memcpy(&magic, data.data(), sizeof magic);
      if (magic == wire::wdbc_magic)
        return read_wdbc(data);
      if (magic == wire::wdb2_magic)
        return read_wdb2(data);
      return make_error(
        ErrorCode::TableMagicUnknown,
        std::format("{}: magic '{}' is not a client-database format wowlib supports for "
                    "client {}.{}.{}.{}",
                    table_name,
                    formats::fourcc_to_string(magic, formats::FourCCEndian::forward),
                    version.major, version.minor, version.patch, version.build));
    }

    /** Load the table from a client filesystem.
        @param fs  the filesystem gateway.
        @param key the file to read.
        @return nothing, or why loading failed. */
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key [[=welder::doc("the file to read")]])
    {
      const auto data = fs.read_file(key);
      if (!data)
        return std::unexpected{data.error()};
      return read(*data);
    }

    /** Serialize the table. A loaded table re-emits the magic it was read
        from; a fresh table uses its client version's canonical .dbc/.db2
        format (write(fs, key) can override by the target path's extension).
        @return the file image, or why encoding failed. */
    [[=welder::doc("Serialize the table to a file image; a loaded table re-emits "
                   "the format it was read from."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const
    {
      if (source_magic_ != 0)
        return write_as(source_magic_);
      const auto magic = fresh_magic(std::nullopt);
      if (!magic)
        return std::unexpected{magic.error()};
      return write_as(*magic);
    }

    /** Serialize the table into a client filesystem (project overlay).
        @param fs  the filesystem gateway.
        @param key the file to write; needs a resolvable path.
        @return nothing, or why saving failed. */
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key [[=welder::doc("the file to write")]]) const
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return make_error(ErrorCode::PathNotResolvable,
                          std::format("saving table {} needs a path for the file key",
                                      table_name));
      auto data = [&]() -> Result<FileBuffer> {
        if (source_magic_ != 0)
          return write_as(source_magic_);
        const auto magic = fresh_magic(*resolved.path);
        if (!magic)
          return std::unexpected{magic.error()};
        return write_as(*magic);
      }();
      if (!data)
        return std::unexpected{data.error()};
      if (auto r = fs.add_file(*resolved.path, *data); !r)
        return std::unexpected{r.error()};
      return {};
    }

    /** The preserved string block the record string fields were decoded from.
        @return the decoded (offset, value) entries. */
    [[=welder::getter,
      =welder::doc("The preserved string block the record string fields were decoded "
                   "from; offsets never move, write() appends new strings past its "
                   "end.")]]
    const formats::StringBlock& strings() const { return strings_; }

  protected:
    /** The canonical on-disk format for a FRESH table of this client version;
        the target path's extension decides in the mixed .dbc/.db2 eras.
        @param path the destination path, when saving through a filesystem.
        @return the magic to encode, or why no format is available. */
    Result<std::uint32_t> fresh_magic(std::optional<std::string_view> path) const
    {
      if (path)
      {
        if (path->ends_with(".dbc"))
          return wire::wdbc_magic;
        if (path->ends_with(".db2") && version < builds::Legion)
          return version < builds::Cata
                   ? make_error(ErrorCode::NotSupported,
                                std::format("{}: .db2 does not exist before Cataclysm",
                                            table_name))
                   : Result<std::uint32_t>{wire::wdb2_magic};
      }
      if (version < builds::Cata)
        return wire::wdbc_magic;
      if (version < builds::Legion)
        return wire::wdb2_magic;
      return make_error(ErrorCode::NotImplemented,
                        std::format("{}: writing fresh Legion+ (WDC) tables is not "
                                    "implemented yet",
                                    table_name));
    }

    /** Encode as @a magic (a loaded table always passes its source magic).
        @param magic the wire format to emit.
        @return the file bytes. */
    Result<FileBuffer> write_as(std::uint32_t magic) const
    {
      if (magic == wire::wdbc_magic)
        return write_wdbc();
      if (magic == wire::wdb2_magic)
        return write_wdb2();
      return make_error(
        ErrorCode::NotImplemented,
        std::format("{}: writing '{}' tables is not implemented yet", table_name,
                    formats::fourcc_to_string(magic, formats::FourCCEndian::forward)));
    }

    /** Decode a WDBC image: 20-byte header, fixed-stride records, string block.
        @param data the whole file content.
        @return nothing, or why the image does not decode. */
    Result<void> read_wdbc(std::span<const std::byte> data)
    {
      if (data.size() < sizeof(wire::WdbcHeader))
        return make_error(ErrorCode::TableTruncated,
                          std::format("{}: {} bytes is too small for a WDBC header",
                                      table_name, data.size()));
      wire::WdbcHeader header;
      std::memcpy(&header, data.data(), sizeof header);

      constexpr std::size_t stride = record_stride<Record>();
      if (header.record_size != stride)
        return make_error(
          ErrorCode::SchemaMismatch,
          std::format("{}: file record_size {} disagrees with the generated schema "
                      "stride {}",
                      table_name, header.record_size, stride));

      const std::size_t expected = sizeof header
                                   + std::size_t{header.record_count} * header.record_size
                                   + header.string_block_size;
      if (data.size() != expected)
        return make_error(
          ErrorCode::TableTruncated,
          std::format("{}: {} bytes on disk, but the header describes {} ({} records of "
                      "{} bytes + {} string bytes)",
                      table_name, data.size(), expected, header.record_count,
                      header.record_size, header.string_block_size));

      records.clear();
      strings_ = {};
      string_offsets_.clear();
      if (auto r = strings_.read(data.subspan(
            sizeof header + std::size_t{header.record_count} * header.record_size,
            header.string_block_size));
          !r)
        return r;

      records.reserve(header.record_count);
      string_offsets_.reserve(std::size_t{header.record_count} * string_slot_count<Record>());
      for (std::uint32_t i = 0; i < header.record_count; ++i)
      {
        Record& record = records.emplace_back();
        decode_record(record, data.subspan(sizeof header + std::size_t{i} * stride, stride));
      }

      source_magic_ = header.magic;
      field_count_ = header.field_count;
      record_size_ = header.record_size;
      return {};
    }

    /** Encode a WDBC image; see the file comment for the byte-perfect policy.
        @return the file bytes. */
    Result<FileBuffer> write_wdbc() const
    {
      constexpr std::size_t stride = record_stride<Record>();
      wire::WdbcHeader header;
      header.record_count = static_cast<std::uint32_t>(records.size());
      header.field_count = source_magic_ != 0 ? field_count_ : field_slot_count<Record>();
      header.record_size = source_magic_ != 0 ? record_size_
                                              : static_cast<std::uint32_t>(stride);

      // Work on a copy: appends for new strings must not mutate the entity
      // (write() is const and repeatable).
      formats::StringBlock block = strings_;
      if (block.empty())
        std::ignore = block.add("");  // Blizzard blocks always lead with a zero byte
      std::unordered_map<std::string, std::uint32_t> lookup;
      for (const formats::StringBlock::Entry& entry : block.entries())
        lookup.try_emplace(entry.value, entry.offset);

      FileBuffer out;
      out.reserve(sizeof header + records.size() * stride + block.size());
      out.resize(sizeof header);
      std::size_t string_cursor = 0;
      for (const Record& record : records)
        encode_record(record, out, block, lookup, string_cursor);

      header.string_block_size = static_cast<std::uint32_t>(block.size());
      if (auto r = block.write(out); !r)
        return std::unexpected{r.error()};
      std::memcpy(out.data(), &header, sizeof header);
      return out;
    }

    /** Decode a WDB2 image: 48-byte header, optional id-index block,
        fixed-stride records, string block, optional trailing copy table. The
        index and copy blocks are preserved verbatim (format unverified — see
        wire/wdb2.hpp).
        @param data the whole file content.
        @return nothing, or why the image does not decode. */
    Result<void> read_wdb2(std::span<const std::byte> data)
    {
      if (data.size() < sizeof(wire::Wdb2Header))
        return make_error(ErrorCode::TableTruncated,
                          std::format("{}: {} bytes is too small for a WDB2 header",
                                      table_name, data.size()));
      wire::Wdb2Header header;
      std::memcpy(&header, data.data(), sizeof header);

      constexpr std::size_t stride = record_stride<Record>();
      if (header.record_size != stride)
        return make_error(
          ErrorCode::SchemaMismatch,
          std::format("{}: file record_size {} disagrees with the generated schema "
                      "stride {}",
                      table_name, header.record_size, stride));
      if (header.max_id != 0 && header.max_id < header.min_id)
        return make_error(ErrorCode::TableTruncated,
                          std::format("{}: WDB2 max_id {} below min_id {}", table_name,
                                      header.max_id, header.min_id));

      const std::size_t index_bytes =
        header.max_id != 0
          ? (std::size_t{header.max_id} - header.min_id + 1) * wire::wdb2_index_entry_bytes
          : 0;
      const std::size_t records_at = sizeof header + index_bytes;
      const std::size_t expected = records_at
                                   + std::size_t{header.record_count} * header.record_size
                                   + header.string_block_size + header.copy_table_size;
      if (data.size() != expected)
        return make_error(
          ErrorCode::TableTruncated,
          std::format("{}: {} bytes on disk, but the header describes {} ({} records of "
                      "{} bytes + {} index + {} string + {} copy-table bytes)",
                      table_name, data.size(), expected, header.record_count,
                      header.record_size, index_bytes, header.string_block_size,
                      header.copy_table_size));

      records.clear();
      strings_ = {};
      string_offsets_.clear();
      if (auto r = strings_.read(data.subspan(
            records_at + std::size_t{header.record_count} * header.record_size,
            header.string_block_size));
          !r)
        return r;

      records.reserve(header.record_count);
      string_offsets_.reserve(std::size_t{header.record_count} * string_slot_count<Record>());
      for (std::uint32_t i = 0; i < header.record_count; ++i)
      {
        Record& record = records.emplace_back();
        decode_record(record, data.subspan(records_at + std::size_t{i} * stride, stride));
      }

      const auto index_block = data.subspan(sizeof header, index_bytes);
      wdb2_index_.assign(index_block.begin(), index_block.end());
      const auto copy_block = data.subspan(expected - header.copy_table_size,
                                           header.copy_table_size);
      wdb2_copy_.assign(copy_block.begin(), copy_block.end());
      wdb2_header_ = header;
      source_magic_ = header.magic;
      field_count_ = header.field_count;
      record_size_ = header.record_size;
      return {};
    }

    /** Encode a WDB2 image; preserved header identity and the verbatim
        index/copy blocks are re-emitted. Rebuilding the id-index block is not
        supported while the format is unverified, so a record-count change on
        an indexed table is an error.
        @return the file bytes. */
    Result<FileBuffer> write_wdb2() const
    {
      constexpr std::size_t stride = record_stride<Record>();
      const bool loaded = source_magic_ == wire::wdb2_magic;
      if (loaded && !wdb2_index_.empty()
          && records.size() != wdb2_header_.record_count)
        return make_error(
          ErrorCode::InvalidEntityState,
          std::format("{}: the WDB2 id-index block cannot be rebuilt yet; adding or "
                      "removing records of an indexed table is unsupported (had {}, "
                      "have {})",
                      table_name, wdb2_header_.record_count, records.size()));

      wire::Wdb2Header header = loaded ? wdb2_header_ : wire::Wdb2Header{};
      header.record_count = static_cast<std::uint32_t>(records.size());
      header.field_count = loaded ? field_count_ : field_slot_count<Record>();
      header.record_size = loaded ? record_size_ : static_cast<std::uint32_t>(stride);
      if (!loaded)
        header.build = version.build;

      formats::StringBlock block = strings_;
      if (block.empty())
        std::ignore = block.add("");
      std::unordered_map<std::string, std::uint32_t> lookup;
      for (const formats::StringBlock::Entry& entry : block.entries())
        lookup.try_emplace(entry.value, entry.offset);

      FileBuffer out;
      out.reserve(sizeof header + wdb2_index_.size() + records.size() * stride
                  + block.size() + wdb2_copy_.size());
      out.resize(sizeof header);
      out.insert(out.end(), wdb2_index_.begin(), wdb2_index_.end());
      std::size_t string_cursor = 0;
      for (const Record& record : records)
        encode_record(record, out, block, lookup, string_cursor);

      header.string_block_size = static_cast<std::uint32_t>(block.size());
      if (auto r = block.write(out); !r)
        return std::unexpected{r.error()};
      out.insert(out.end(), wdb2_copy_.begin(), wdb2_copy_.end());
      header.copy_table_size = static_cast<std::uint32_t>(wdb2_copy_.size());
      std::memcpy(out.data(), &header, sizeof header);
      return out;
    }

  private:
    /** Decode one record image into @a record, walking the schema members in
        declaration order (noninline members hold no record bytes and are
        skipped).
        @param record the destination record.
        @param image  exactly record_stride<Record>() bytes. */
    void decode_record(Record& record, std::span<const std::byte> image)
    {
      static constexpr auto members = detail::record_members<Record>();
      std::size_t pos = 0;
      template for (constexpr auto m : members)
      {
        if constexpr (!detail::annotation<detail::noninline_spec, m>().has_value())
          read_field(record.[:m:], image, pos);
      }
    }

    /** Encode one record, appending its image to @a out.
        @param record  the record to encode.
        @param out     the destination buffer.
        @param block   the string block new strings append to.
        @param lookup  value -> offset dedup index over @a block.
        @param cursor  the running index into the original-offset journal. */
    void encode_record(const Record& record, FileBuffer& out, formats::StringBlock& block,
                       std::unordered_map<std::string, std::uint32_t>& lookup,
                       std::size_t& cursor) const
    {
      static constexpr auto members = detail::record_members<Record>();
      template for (constexpr auto m : members)
      {
        if constexpr (!detail::annotation<detail::noninline_spec, m>().has_value())
          write_field(record.[:m:], out, block, lookup, cursor);
      }
    }

    /** Read a scalar field (integer or float) off the record image. */
    template <typename T>
      requires ((std::integral<T> && !std::same_as<T, bool>) || std::floating_point<T>)
    void read_field(T& out, std::span<const std::byte> image, std::size_t& pos)
    {
      std::memcpy(&out, image.data() + pos, sizeof(T));
      pos += sizeof(T);
    }

    /** Read a string field: a u32 string-block offset, journaled for the
        byte-perfect write-back. */
    void read_field(std::string& out, std::span<const std::byte> image, std::size_t& pos)
    {
      std::uint32_t offset = 0;
      std::memcpy(&offset, image.data() + pos, sizeof offset);
      pos += sizeof offset;
      out = strings_.at(offset);
      string_offsets_.push_back(offset);
    }

    /** Read a localized string column: Langs slot offsets, then the flags. */
    template <std::size_t Langs>
    void read_field(LocString<Langs>& out, std::span<const std::byte> image, std::size_t& pos)
    {
      for (std::string& value : out.values)
        read_field(value, image, pos);
      read_field(out.flags, image, pos);
    }

    /** Read an array column element-wise. */
    template <typename T, std::size_t N>
    void read_field(std::array<T, N>& out, std::span<const std::byte> image, std::size_t& pos)
    {
      for (T& element : out)
        read_field(element, image, pos);
    }

    /** Append a scalar field (integer or float) to the record image. */
    template <typename T>
      requires ((std::integral<T> && !std::same_as<T, bool>) || std::floating_point<T>)
    void write_field(const T& value, FileBuffer& out, formats::StringBlock&,
                     std::unordered_map<std::string, std::uint32_t>&, std::size_t&) const
    {
      const auto* bytes = reinterpret_cast<const std::byte*>(&value);
      out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    /** Append a string field as its resolved string-block offset. */
    void write_field(const std::string& value, FileBuffer& out, formats::StringBlock& block,
                     std::unordered_map<std::string, std::uint32_t>& lookup,
                     std::size_t& cursor) const
    {
      const std::uint32_t offset = resolve_string(value, block, lookup, cursor);
      write_field(offset, out, block, lookup, cursor);
    }

    /** Append a localized string column: every slot offset, then the flags. */
    template <std::size_t Langs>
    void write_field(const LocString<Langs>& value, FileBuffer& out,
                     formats::StringBlock& block,
                     std::unordered_map<std::string, std::uint32_t>& lookup,
                     std::size_t& cursor) const
    {
      for (const std::string& slot : value.values)
        write_field(slot, out, block, lookup, cursor);
      write_field(value.flags, out, block, lookup, cursor);
    }

    /** Append an array column element-wise. */
    template <typename T, std::size_t N>
    void write_field(const std::array<T, N>& value, FileBuffer& out,
                     formats::StringBlock& block,
                     std::unordered_map<std::string, std::uint32_t>& lookup,
                     std::size_t& cursor) const
    {
      for (const T& element : value)
        write_field(element, out, block, lookup, cursor);
    }

    /** The string-block offset a string field writes: the journaled original
        offset while the value still matches it (byte-perfect round-trip,
        shared-tail references included), else the offset of an equal existing
        entry, else a fresh append.
        @param value  the field value.
        @param block  the (copied) string block to resolve against.
        @param lookup value -> offset dedup index over @a block, updated on append.
        @param cursor the running index into the original-offset journal.
        @return the offset to store in the record image. */
    std::uint32_t resolve_string(const std::string& value, formats::StringBlock& block,
                                 std::unordered_map<std::string, std::uint32_t>& lookup,
                                 std::size_t& cursor) const
    {
      const std::size_t slot = cursor++;
      if (slot < string_offsets_.size())
      {
        const std::uint32_t original = string_offsets_[slot];
        if (block.at(original) == value)
          return original;
      }
      if (const auto it = lookup.find(value); it != lookup.end())
        return it->second;
      const std::uint32_t offset = block.add(value);
      lookup.emplace(value, offset);
      return offset;
    }

    std::uint32_t source_magic_ = 0;  /**< The magic read() sniffed; 0 for a fresh table. */
    std::uint32_t field_count_ = 0;   /**< Preserved header field_count (not always derivable). */
    std::uint32_t record_size_ = 0;   /**< Preserved header record_size. */
    formats::StringBlock strings_;    /**< Preserved string block; offsets never move. */
    wire::Wdb2Header wdb2_header_{};  /**< Preserved WDB2 header identity fields. */
    FileBuffer wdb2_index_;           /**< Preserved WDB2 id-index block, verbatim. */
    FileBuffer wdb2_copy_;            /**< Preserved WDB2 copy table, verbatim. */

    /** The original string-block offset of every string field, row-major in
        record then schema order — the byte-perfect write-back journal. */
    std::vector<std::uint32_t> string_offsets_;
  };
}
