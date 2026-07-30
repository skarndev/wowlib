#pragma once

/** @file
    Table<Record> — the client-database entity: a typed record vector decoded
    from / encoded to the on-disk table formats. It is a THIN facade: it owns the
    records and the preserved decode state (TableState) and sniffs the format,
    but the byte-level work lives in the non-templated per-format codecs
    (wdbc.{hpp,cpp}, wdb2.{hpp,cpp}, wdc/), which drive the records through the
    record bridge (record_bridge.hpp) so they compile once rather than once per
    generated table. Speaks WDBC (every pre-Cataclysm .dbc), WDB2 (the
    Cata..WoD .db2), and the whole WDC family — WDC1 (Legion), WDC3 (BfA ..
    early DF), WDC4 and WDC5 (DF/TWW) — reading and writing canonically.

    Round-trip policy (plan of record, 2026-07-29): WDBC and WDB2 are
    byte-perfect (the string block preserves decoded offsets); WDC writes are
    canonical re-encodes with the semantic guarantee (write -> re-read decodes to
    identical values). Encrypted sections always pass through verbatim. */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/db/codec.hpp>
#include <wowlib/db/record_bridge.hpp>
#include <wowlib/db/schema.hpp>
#include <wowlib/db/wdb2.hpp>
#include <wowlib/db/wdbc.hpp>
#include <wowlib/db/wdc/wdc.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::db
{
  /** A client database table: the typed records of one DBFilesClient file.

      The record type is generated from WoWDBDefs by dbdgen (a flat struct whose
      member types carry the column shapes — schema.hpp) and pins both the client
      version and the table identity, so `Table<MapRecord<V>>` IS the Map table of
      client V.

      NOT welded: it is a non-welded MIXIN whose public members flatten onto a
      per-table welded wrapper the bindings generate (like ChunkedFile under
      WMORoot). The member annotations here (getters, no_reassign, docs) are read
      off this declaring class during that flatten.
      @tparam Record the generated record type. */
  template <TableRecord Record>
  class Table
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
      TypedRecordSink<Record> sink{records};
      if (magic == wdbc_magic)
        return read_wdbc(info(), data, sink, state_);
      if (magic == wdb2_magic)
        return read_wdb2(info(), data, sink, state_);
      if (wdc::is_wdc_magic(magic))
        return wdc::read_wdc(info(), data, sink, state_);
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

    /** Serialize the table. A loaded table re-emits the magic it was read from; a
        fresh table uses its client version's canonical .dbc/.db2 format
        (write(fs, key) can override by the target path's extension).
        @param policy how keyless encrypted sections are handled (WDC only).
        @return the file image, or why encoding failed. */
    [[=welder::doc("Serialize the table to a file image; a loaded table re-emits "
                   "the format it was read from. `policy` decides how keyless "
                   "encrypted sections are handled."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write(EncryptedPolicy policy
                             [[=welder::doc("keyless-section handling (WDC only)")]]
                             = EncryptedPolicy::Preserve) const
    {
      if (state_.source_magic != 0)
        return write_as(state_.source_magic, policy);
      const auto magic = fresh_magic(std::nullopt);
      if (!magic)
        return std::unexpected{magic.error()};
      return write_as(*magic, policy);
    }

    /** Serialize the table into a client filesystem (project overlay).
        @param fs     the filesystem gateway.
        @param key    the file to write; needs a resolvable path.
        @param policy how keyless encrypted sections are handled (see write()).
        @return nothing, or why saving failed. */
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key [[=welder::doc("the file to write")]],
                       EncryptedPolicy policy
                       [[=welder::doc("keyless-section handling (WDC only)")]]
                       = EncryptedPolicy::Preserve) const
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return make_error(ErrorCode::PathNotResolvable,
                          std::format("saving table {} needs a path for the file key",
                                      table_name));
      auto data = [&]() -> Result<FileBuffer> {
        if (state_.source_magic != 0)
          return write_as(state_.source_magic, policy);
        const auto magic = fresh_magic(*resolved.path);
        if (!magic)
          return std::unexpected{magic.error()};
        return write_as(*magic, policy);
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
    const formats::StringBlock& strings() const { return state_.strings; }

    /** The encrypted sections skipped on the last read (empty when the file was
        fully decodable or is not a WDC format).
        @return the encrypted-section reports. */
    [[=welder::getter,
      =welder::doc("The encrypted sections skipped on read: their records are not "
                   "in records, but the file re-writes them verbatim.")]]
    const std::vector<EncryptedSection>& encrypted_sections() const { return state_.encrypted; }

    /** Whether every record of the file was decoded (no encrypted sections).
        @return true when records holds the whole table. */
    [[=welder::getter,
      =welder::doc("Whether the whole table decoded — false when encrypted sections "
                   "were skipped.")]]
    bool fully_decoded() const { return state_.encrypted.empty(); }

  private:
    /** The table identity + schema the codecs work from. */
    static TableInfo info()
    {
      static constexpr auto schema = schema_of<Record>();
      return TableInfo{version, table_name, schema};
    }

    /** The canonical .db2 flavor of this client version (the format a fresh
        Cataclysm-or-later table is written in).
        @return the magic to encode. */
    static constexpr std::uint32_t db2_magic_for_version()
    {
      if (version < builds::Legion)
        return wdb2_magic;
      if (version < builds::BfA)
        return wdc::wdc1_magic;
      if (version < ClientVersion{10, 1, 0, 48480})
        return wdc::wdc3_magic;
      if (version < ClientVersion{10, 2, 5, 52432})
        return wdc::wdc4_magic;
      return wdc::wdc5_magic;
    }

    /** The canonical on-disk format for a FRESH table of this client version; the
        target path's extension decides in the mixed .dbc/.db2 eras.
        @param path the destination path, when saving through a filesystem.
        @return the magic to encode, or why no format is available. */
    Result<std::uint32_t> fresh_magic(std::optional<std::string_view> path) const
    {
      if (path)
      {
        if (path->ends_with(".dbc"))
          return wdbc_magic;
        if (path->ends_with(".db2"))
          return version < builds::Cata
                   ? make_error(ErrorCode::NotSupported,
                                std::format("{}: .db2 does not exist before Cataclysm",
                                            table_name))
                   : Result<std::uint32_t>{db2_magic_for_version()};
      }
      if (version < builds::Cata)
        return wdbc_magic;
      return db2_magic_for_version();
    }

    /** Encode as @a magic (a loaded table always passes its source magic). */
    Result<FileBuffer> write_as(std::uint32_t magic, EncryptedPolicy policy) const
    {
      TypedRecordSource<Record> source{records};
      if (magic == wdbc_magic)
        return write_wdbc(info(), source, state_);
      if (magic == wdb2_magic)
        return write_wdb2(info(), source, state_);
      if (wdc::is_wdc_magic(magic))
        return wdc::write_wdc(magic, info(), source, state_, policy);
      return make_error(
        ErrorCode::NotImplemented,
        std::format("{}: writing '{}' tables is not implemented yet", table_name,
                    formats::fourcc_to_string(magic, formats::FourCCEndian::forward)));
    }

    TableState state_;
  };
}
