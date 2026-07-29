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

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
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
#include <wowlib/db/wire/wdc3.hpp>
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
      if (magic == wire::wdc3_magic)
        return read_wdc3(data);
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

    /** One WDC3 section whose records could not be decoded because they are
        encrypted under a TACT key wowlib does not hold. */
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An encrypted WDC section: its records are behind a TACT key "
                   "wowlib does not hold, so they are absent from records.")
    ]] EncryptedSection
    {
      [[=welder::doc("The section's TACT key lookup hash (names the missing key).")]]
      std::uint64_t key_hash = 0;

      [[=welder::doc("How many records the encrypted section holds.")]]
      std::uint32_t record_count = 0;

      [[=welder::doc("The ids of the encrypted records, when the file lists them.")]]
      std::vector<std::uint32_t> ids;
    };

    /** The encrypted sections skipped on the last read (empty when the file was
        fully decodable or is not a WDC format).
        @return the encrypted-section reports. */
    [[=welder::getter,
      =welder::doc("The encrypted sections skipped on read: their records are not "
                   "in records, but the file re-writes them verbatim.")]]
    const std::vector<EncryptedSection>& encrypted_sections() const { return encrypted_; }

    /** Whether every record of the file was decoded (no encrypted sections).
        @return true when records holds the whole table. */
    [[=welder::getter,
      =welder::doc("Whether the whole table decoded — false when encrypted sections "
                   "were skipped.")]]
    bool fully_decoded() const { return encrypted_.empty(); }

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
      encrypted_.clear();
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
      encrypted_.clear();
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

    /** Decode a WDC3 image onto the generated schema. Records of every
        unencrypted section are decoded (inline fields via their compression
        kind, non-inline id from the id_list, non-inline relation from the
        relationship block, strings resolved through the WDC2+ relative offset);
        copy-table rows are materialized as clones. Encrypted sections are
        located and reported (encrypted_sections()) but not decoded. Reading is
        the only WDC operation stage 3 provides — writing WDC is a later stage.
        @param data the whole file content.
        @return nothing, or why the image does not decode. */
    Result<void> read_wdc3(std::span<const std::byte> data)
    {
      // WDC3 is a Legion+ format; pre-Cata records carry LocString columns the
      // WDC decoders don't model. Those clients never ship a WDC3 file (read()
      // dispatches by magic), so the decode body is compiled only for Cata+
      // records — keeping the LocString overloads out of the WDC path.
      if constexpr (version < builds::Cata)
        return make_error(
          ErrorCode::TableMagicUnknown,
          std::format("{}: a {}.{} client does not use the WDC3 format", table_name,
                      version.major, version.minor));
      else
      {
      auto parsed = wire::Wdc3Image::parse(data);
      if (!parsed)
        return std::unexpected{parsed.error()};
      const wire::Wdc3Image& img = *parsed;

      constexpr std::size_t inline_columns = wdc_inline_column_count();
      if (img.header.field_count != inline_columns)
        return make_error(
          ErrorCode::SchemaMismatch,
          std::format("{}: WDC3 stores {} inline fields but the generated schema has {} "
                      "(layout_hash {:#010x})",
                      table_name, img.header.field_count, inline_columns, img.header.layout_hash));

      records.clear();
      encrypted_.clear();
      strings_ = {};
      source_magic_ = img.header.magic;

      const auto additional = img.field_additional_offsets();
      for (const wire::Wdc3Section& sec : img.sections)
      {
        if (sec.encrypted)
        {
          EncryptedSection report{.key_hash = sec.header.tact_key_hash,
                                  .record_count = sec.header.record_count};
          const auto ids = std::span{reinterpret_cast<const std::uint32_t*>(sec.id_list.data()),
                                     sec.id_list.size() / 4};
          report.ids.assign(ids.begin(), ids.end());
          encrypted_.push_back(std::move(report));
          continue;
        }
        if (img.is_sparse())
          return make_error(ErrorCode::NotImplemented,
                            std::format("{}: WDC3 sparse/offset-map tables are not decoded "
                                        "yet (rare — 4 of 835 in the 9.2.7 corpus)",
                                        table_name));

        const std::size_t stride = img.header.record_size;
        for (std::uint32_t r = 0; r < sec.header.record_count; ++r)
        {
          const std::uint32_t id = wdc_record_id(img, sec, r);
          const auto record_bytes = sec.records.subspan(std::size_t{r} * stride, stride);
          const std::uint64_t record_file_offset =
            std::size_t{sec.header.file_offset} + std::size_t{r} * stride;
          Record& record = records.emplace_back();
          decode_wdc3_record(record, img, record_bytes, record_file_offset, id, additional);
        }
        // Copy table: each {new_id, src_id} clones the source record with a new id.
        for (std::size_t c = 0; c + 8 <= sec.copy_table.size(); c += 8)
        {
          std::uint32_t new_id = 0, src_id = 0;
          std::memcpy(&new_id, sec.copy_table.data() + c, 4);
          std::memcpy(&src_id, sec.copy_table.data() + c + 4, 4);
          if (auto* src = find_by_id(src_id))
          {
            Record clone = *src;
            set_id(clone, new_id);
            records.push_back(std::move(clone));
          }
        }
      }
      return {};
      }
    }

  private:
    /** The number of INLINE schema columns (those the WDC record stores as
        fields — everything not $noninline$). */
    static consteval std::size_t wdc_inline_column_count()
    {
      std::size_t n = 0;
      for (const Column& col : schema_of<Record>())
        n += col.noninline ? 0 : 1;
      return n;
    }

    /** The id of section record @a r: the id_list entry when the id is
        non-inline (flag 0x04), else the inline id column decoded from the
        record. */
    std::uint32_t wdc_record_id(const wire::Wdc3Image& img, const wire::Wdc3Section& sec,
                                std::uint32_t r) const
    {
      if (img.id_is_noninline())
      {
        if (std::size_t{r} * 4 + 4 <= sec.id_list.size())
        {
          std::uint32_t id = 0;
          std::memcpy(&id, sec.id_list.data() + std::size_t{r} * 4, 4);
          return id;
        }
        return 0;
      }
      // Inline id: the id_index'th inline field, read as a plain unsigned int.
      const auto record_bytes = sec.records.subspan(std::size_t{r} * img.header.record_size,
                                                    img.header.record_size);
      const auto additional = img.field_additional_offsets();
      return static_cast<std::uint32_t>(
        img.field_raw(img.header.id_index, 0, 1, record_bytes, 0, additional));
    }

    /** Decode one record's members from the WDC image. Mirrors the member walk
        of decode_record but pulls each inline column from the field decoder and
        the non-inline id/relation from the satellites.
        @param record             the destination record.
        @param img                the parsed image.
        @param record_bytes       this record's byte span.
        @param record_file_offset the record's absolute file offset (strings need it).
        @param id                 the record's id.
        @param additional         field_additional_offsets(). */
    void decode_wdc3_record(Record& record, const wire::Wdc3Image& img,
                            std::span<const std::byte> record_bytes,
                            std::uint64_t record_file_offset, std::uint32_t id,
                            const std::vector<std::uint32_t>& additional)
    {
      static constexpr auto members = detail::record_members<Record>();
      std::size_t field = 0;
      template for (constexpr auto m : members)
      {
        constexpr bool noninline = detail::annotation<detail::noninline_spec, m>().has_value();
        constexpr bool is_id = detail::annotation<detail::id_spec, m>().has_value();
        if constexpr (noninline)
        {
          if constexpr (is_id)
            record.[:m:] = static_cast<std::remove_cvref_t<decltype(record.[:m:])>>(id);
          // Non-inline relations resolve from the relationship block; left at
          // the member default when the block has no entry (rare in WDC3).
        }
        else
        {
          read_wdc3_field(record.[:m:], img, field, record_bytes, record_file_offset, id,
                          additional);
          ++field;
        }
      }
    }

    /** Decode one inline integer field into a scalar member. */
    template <typename T>
      requires ((std::integral<T> && !std::same_as<T, bool>))
    void read_wdc3_field(T& out, const wire::Wdc3Image& img, std::size_t field,
                         std::span<const std::byte> record_bytes, std::uint64_t, std::uint32_t id,
                         const std::vector<std::uint32_t>& additional)
    {
      out = static_cast<T>(wdc_signed_fit<T>(
        img.field_raw(field, 0, 1, record_bytes, id, additional),
        img.elem_bit_width(field, 1), img.field_is_signed(field) || std::is_signed_v<T>));
    }

    /** Decode one inline float field. */
    void read_wdc3_field(float& out, const wire::Wdc3Image& img, std::size_t field,
                         std::span<const std::byte> record_bytes, std::uint64_t, std::uint32_t id,
                         const std::vector<std::uint32_t>& additional)
    {
      const auto bits = static_cast<std::uint32_t>(img.field_raw(field, 0, 1, record_bytes, id,
                                                                 additional));
      out = std::bit_cast<float>(bits);
    }

    /** Decode one inline string field: the stored value is the offset from the
        field's own file position to the string (WDC2+ relative offsets). */
    void read_wdc3_field(std::string& out, const wire::Wdc3Image& img, std::size_t field,
                         std::span<const std::byte> record_bytes, std::uint64_t record_file_offset,
                         std::uint32_t id, const std::vector<std::uint32_t>& additional)
    {
      const std::uint32_t rel = static_cast<std::uint32_t>(
        img.field_raw(field, 0, 1, record_bytes, id, additional));
      const std::size_t field_byte = img.field_storage[field].field_offset_bits / 8;
      const std::size_t at = record_file_offset + field_byte + rel;
      out = read_c_string(img.file, at);
    }

    /** Decode an inline array column element by element. */
    template <typename T, std::size_t N>
    void read_wdc3_field(std::array<T, N>& out, const wire::Wdc3Image& img, std::size_t field,
                         std::span<const std::byte> record_bytes, std::uint64_t record_file_offset,
                         std::uint32_t id, const std::vector<std::uint32_t>& additional)
    {
      for (std::uint32_t e = 0; e < N; ++e)
      {
        if constexpr (std::same_as<T, float>)
          out[e] = std::bit_cast<float>(static_cast<std::uint32_t>(
            img.field_raw(field, e, N, record_bytes, id, additional)));
        else if constexpr (std::same_as<T, std::string>)
        {
          const std::uint32_t rel = static_cast<std::uint32_t>(
            img.field_raw(field, e, N, record_bytes, id, additional));
          const std::size_t field_byte = img.field_storage[field].field_offset_bits / 8;
          out[e] = read_c_string(img.file, record_file_offset + field_byte + rel);
        }
        else
          out[e] = static_cast<T>(wdc_signed_fit<T>(
            img.field_raw(field, e, N, record_bytes, id, additional),
            img.elem_bit_width(field, N), img.field_is_signed(field) || std::is_signed_v<T>));
      }
    }

    /** Sign-extend @a raw from @a bits to a full width when @a is_signed and the
        sign bit is set; otherwise return it unchanged.
        @tparam T        the destination integer type.
        @param raw       the zero-extended field bits.
        @param bits      the field's element bit width.
        @param is_signed whether the column/compression is signed.
        @return the value to store (as an unsigned carrier; the caller casts). */
    template <typename T>
    static std::uint64_t wdc_signed_fit(std::uint64_t raw, std::size_t bits, bool is_signed)
    {
      if (!is_signed || bits == 0 || bits >= 64)
        return raw;
      const std::uint64_t sign_bit = std::uint64_t{1} << (bits - 1);
      if (raw & sign_bit)
        return raw | ~((std::uint64_t{1} << bits) - 1);
      return raw;
    }

    /** Read a NUL-terminated string starting at absolute file offset @a at.
        @param file the whole file span.
        @param at   the absolute byte offset.
        @return the string (empty when out of range or empty). */
    static std::string read_c_string(std::span<const std::byte> file, std::size_t at)
    {
      if (at >= file.size())
        return {};
      const auto* bytes = reinterpret_cast<const char*>(file.data());
      std::size_t end = at;
      while (end < file.size() && bytes[end] != '\0')
        ++end;
      return std::string{bytes + at, end - at};
    }

    /** The first decoded record whose id column equals @a id, or nullptr. */
    Record* find_by_id(std::uint32_t id)
    {
      static constexpr auto members = detail::record_members<Record>();
      for (Record& record : records)
      {
        bool match = false;
        template for (constexpr auto m : members)
          if constexpr (detail::annotation<detail::id_spec, m>().has_value())
            match = static_cast<std::uint32_t>(record.[:m:]) == id;
        if (match)
          return &record;
      }
      return nullptr;
    }

    /** Set the id column of @a record to @a id (copy-table clones). */
    void set_id(Record& record, std::uint32_t id)
    {
      static constexpr auto members = detail::record_members<Record>();
      template for (constexpr auto m : members)
        if constexpr (detail::annotation<detail::id_spec, m>().has_value())
          record.[:m:] = static_cast<std::remove_cvref_t<decltype(record.[:m:])>>(id);
    }

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
    std::vector<EncryptedSection> encrypted_; /**< Encrypted WDC sections skipped on read. */

    /** The original string-block offset of every string field, row-major in
        record then schema order — the byte-perfect write-back journal. */
    std::vector<std::uint32_t> string_offsets_;
  };
}
