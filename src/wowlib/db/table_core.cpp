/** @file
    TableCore's method bodies — the whole table engine, compiled once for all
    generated tables (see table_core.hpp for why). These are Table<Record>'s
    old bodies with the consteval identity facts (version, name, schema,
    has-id) read from the runtime TableInfo instead. */

#include <wowlib/db/table_core.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <unordered_map>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/db/wdb2.hpp>
#include <wowlib/db/wdbc.hpp>
#include <wowlib/db/wdc/wdc.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::db
{
  Result<void> TableCore::require_wired() const
  {
    if ((vec_ && ops_) || (ext_sink_ && ext_source_))
      return {};
    return make_error(ErrorCode::InvalidEntityState,
                      "this table base carries no records: construct a concrete "
                      "table class, not the TableBase supertype");
  }

  Result<void> TableCore::read(std::span<const std::byte> data)
  {
    if (auto w = require_wired(); !w)
      return w;
    if (data.size() < sizeof(std::uint32_t))
      return make_error(ErrorCode::TableTruncated,
                        std::format("{}: {} bytes is too small for a client database",
                                    info_.name, data.size()));
    std::uint32_t magic = 0;
    std::memcpy(&magic, data.data(), sizeof magic);
    // Either wiring: the codecs only ever see the RecordSink interface.
    ErasedRecordSink erased{vec_, ops_};
    RecordSink& sink = ext_sink_ ? *ext_sink_ : static_cast<RecordSink&>(erased);
    if (magic == wdbc_magic)
      return read_wdbc(info_, data, sink, state_);
    if (magic == wdb2_magic)
      return read_wdb2(info_, data, sink, state_);
    if (wdc::is_wdc_magic(magic))
      return wdc::read_wdc(info_, data, sink, state_);
    return make_error(
      ErrorCode::TableMagicUnknown,
      std::format("{}: magic '{}' is not a client-database format wowlib supports for "
                  "client {}.{}.{}.{}",
                  info_.name,
                  formats::fourcc_to_string(magic, formats::FourCCEndian::forward),
                  info_.version.major, info_.version.minor, info_.version.patch,
                  info_.version.build));
  }

  Result<void> TableCore::read(fs::FileSystem& fs, const FileKey& key)
  {
    if (auto w = require_wired(); !w)
      return w;
    const auto data = fs.read_file(key);
    if (!data)
      return std::unexpected{data.error()};
    return read(*data);
  }

  Result<FileBuffer> TableCore::write(EncryptedPolicy policy) const
  {
    if (auto w = require_wired(); !w)
      return std::unexpected{w.error()};
    if (state_.source_magic != 0)
      return write_as(state_.source_magic, policy);
    const auto magic = fresh_magic(std::nullopt);
    if (!magic)
      return std::unexpected{magic.error()};
    return write_as(*magic, policy);
  }

  Result<void> TableCore::write(fs::FileSystem& fs, const FileKey& key,
                                EncryptedPolicy policy) const
  {
    if (auto w = require_wired(); !w)
      return w;
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        std::format("saving table {} needs a path for the file key",
                                    info_.name));
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

  formats::ValidationReport TableCore::validate() const
  {
    formats::ValidationReport report;
    if (!require_wired())
      return report;  // an unwired base holds nothing to violate
    const std::span<const Column> schema = info_.schema;
    const ErasedRecordSource erased{vec_, ops_};
    const RecordSource& source =
      ext_source_ ? *ext_source_ : static_cast<const RecordSource&>(erased);

    // Plenty of client tables are KEYLESS — pure lookup rows with no $id$
    // column. No primary key to keep unique there.
    const bool has_id =
      std::ranges::any_of(schema, [](const Column& c) { return c.is_id; });
    std::unordered_map<std::uint32_t, std::size_t> first_seen;
    const std::size_t n = source.size();
    for (std::size_t r = 0; r < n && !report.full(); ++r)
    {
      for (std::size_t c = 0; c < schema.size(); ++c)
      {
        const Column& column = schema[c];
        const std::size_t slots = column.string_slots();
        for (std::size_t e = 0; e < slots; ++e)
          if (source.get_string(r, c, e).find('\0') != std::string_view::npos)
            report.add_error(slots > 1 ? std::format("records[{}].{}[{}]", r,
                                                     column.name_view(), e)
                                       : std::format("records[{}].{}", r,
                                                     column.name_view()),
                             "the string holds an embedded NUL; the string block "
                             "terminates entries with it, so it would read back truncated");
      }

      if (has_id)
      {
        const std::uint32_t id = source.id_of(r);
        if (const auto [at, fresh] = first_seen.try_emplace(id, r); !fresh)
          report.add_error(std::format("records[{}]", r),
                           std::format("duplicate id {} (already used by records[{}]); the "
                                       "client indexes records by id",
                                       id, at->second));
      }
    }
    return report;
  }

  std::uint32_t TableCore::db2_magic_for_version() const
  {
    const ClientVersion& v = info_.version;
    if (v < builds::Legion)
      return wdb2_magic;
    if (v < builds::BfA)
      return wdc::wdc1_magic;
    if (v < ClientVersion{10, 1, 0, 48480})
      return wdc::wdc3_magic;
    if (v < ClientVersion{10, 2, 5, 52432})
      return wdc::wdc4_magic;
    return wdc::wdc5_magic;
  }

  Result<std::uint32_t> TableCore::fresh_magic(
    std::optional<std::string_view> path) const
  {
    if (path)
    {
      if (path->ends_with(".dbc"))
        return wdbc_magic;
      if (path->ends_with(".db2"))
        return info_.version < builds::Cata
                 ? make_error(ErrorCode::NotSupported,
                              std::format("{}: .db2 does not exist before Cataclysm",
                                          info_.name))
                 : Result<std::uint32_t>{db2_magic_for_version()};
    }
    if (info_.version < builds::Cata)
      return wdbc_magic;
    return db2_magic_for_version();
  }

  Result<FileBuffer> TableCore::write_as(std::uint32_t magic,
                                         EncryptedPolicy policy) const
  {
    ErasedRecordSource erased{vec_, ops_};
    const RecordSource& source =
      ext_source_ ? *ext_source_ : static_cast<const RecordSource&>(erased);
    if (magic == wdbc_magic)
      return write_wdbc(info_, source, state_);
    if (magic == wdb2_magic)
      return write_wdb2(info_, source, state_);
    if (wdc::is_wdc_magic(magic))
      return wdc::write_wdc(magic, info_, source, state_, policy);
    return make_error(
      ErrorCode::NotImplemented,
      std::format("{}: writing '{}' tables is not implemented yet", info_.name,
                  formats::fourcc_to_string(magic, formats::FourCCEndian::forward)));
  }
}
