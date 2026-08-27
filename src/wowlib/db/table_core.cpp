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

namespace wowlib::db {
  Result<void> TableCore::_requireWired() const {
    if ((_vec && _ops) || (_extSink && _extSource)) return {};
    return makeError(ErrorCode::InvalidEntityState,
                      "this table base carries no records: construct a concrete "
                      "table class, not the TableBase supertype");
  }

  Result<void> TableCore::read(std::span<const std::byte> data) {
    if (auto w = _requireWired(); !w) return w;
    if (data.size() < sizeof(std::uint32_t))
      return makeError(ErrorCode::TableTruncated,
                        std::format("{}: {} bytes is too small for a client database", _info.name, data.size()));
    std::uint32_t magic = 0;
    std::memcpy(&magic, data.data(), sizeof magic);
    // Either wiring: the codecs only ever see the RecordSink interface.
    ErasedRecordSink erased{_vec, _ops};
    RecordSink& sink = _extSink ? *_extSink : static_cast<RecordSink&>(erased);
    if (magic == WdbcMagic) return readWdbc(_info, data, sink, _state);
    if (magic == Wdb2Magic) return readWdb2(_info, data, sink, _state);
    if (wdc::isWdcMagic(magic)) return wdc::readWdc(_info, data, sink, _state);
    return makeError(ErrorCode::TableMagicUnknown, std::format(
                        "{}: magic '{}' is not a client-database format wowlib supports for " "client {}.{}.{}.{}",
                        _info.name, formats::fourccToString(magic, formats::FourCCEndian::Forward),
                        _info.version.major, _info.version.minor, _info.version.patch, _info.version.build));
  }

  Result<void> TableCore::read(fs::FileSystem& fs, const FileKey& key) {
    if (auto w = _requireWired(); !w) return w;
    const auto data = fs.readFile(key);
    if (!data) return std::unexpected{data.error()};
    return read(*data);
  }

  Result<FileBuffer> TableCore::write(EncryptedPolicy policy) const {
    if (auto w = _requireWired(); !w) return std::unexpected{w.error()};
    if (_state.sourceMagic != 0) return _writeAs(_state.sourceMagic, policy);
    const auto magic = _freshMagic(std::nullopt);
    if (!magic) return std::unexpected{magic.error()};
    return _writeAs(*magic, policy);
  }

  Result<void> TableCore::write(fs::FileSystem& fs, const FileKey& key, EncryptedPolicy policy) const {
    if (auto w = _requireWired(); !w) return w;
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return makeError(ErrorCode::PathNotResolvable,
                        std::format("saving table {} needs a path for the file key", _info.name));
    auto data = [&]() -> Result<FileBuffer> {
      if (_state.sourceMagic != 0) return _writeAs(_state.sourceMagic, policy);
      const auto magic = _freshMagic(*resolved.path);
      if (!magic) return std::unexpected{magic.error()};
      return _writeAs(*magic, policy);
    }();
    if (!data) return std::unexpected{data.error()};
    if (auto r = fs.addFile(*resolved.path, *data); !r) return std::unexpected{r.error()};
    return {};
  }

  formats::ValidationReport TableCore::validate() const {
    formats::ValidationReport report;
    if (!_requireWired()) return report; // an unwired base holds nothing to violate
    const std::span<const Column> schema = _info.schema;
    const ErasedRecordSource erased{_vec, _ops};
    const RecordSource& source = _extSource ? *_extSource : static_cast<const RecordSource&>(erased);

    // Plenty of client tables are KEYLESS — pure lookup rows with no $id$
    // column. No primary key to keep unique there.
    const bool hasId = std::ranges::any_of(schema, [](const Column& c) { return c.isId; });
    std::unordered_map<std::uint32_t, std::size_t> firstSeen;
    const std::size_t n = source.size();
    for (std::size_t r = 0; r < n && !report.full(); ++r) {
      for (std::size_t c = 0; c < schema.size(); ++c) {
        const Column& column = schema[c];
        const std::size_t slots = column.stringSlots();
        for (std::size_t e = 0; e < slots; ++e)
          if (source.getString(r, c, e).find('\0') != std::string_view::npos)
            report.addError(
              slots > 1
                ? std::format("records[{}].{}[{}]", r, column.nameView(), e)
                : std::format("records[{}].{}", r, column.nameView()),
              "the string holds an embedded NUL; the string block "
              "terminates entries with it, so it would read back truncated");
      }

      if (hasId) {
        const std::uint32_t recordId = source.idOf(r);
        if (const auto [at, fresh] = firstSeen.try_emplace(recordId, r); !fresh)
          report.addError(std::format("records[{}]", r),
                           std::format(
                             "duplicate id {} (already used by records[{}]); the " "client indexes records by id",
                             recordId, at->second));
      }
    }
    return report;
  }

  std::uint32_t TableCore::_db2MagicForVersion() const {
    const ClientVersion& v = _info.version;
    if (v < builds::Legion) return Wdb2Magic;
    if (v < builds::BfA) return wdc::Wdc1Magic;
    if (v < ClientVersion{10, 1, 0, 48480}) return wdc::Wdc3Magic;
    if (v < ClientVersion{10, 2, 5, 52432}) return wdc::Wdc4Magic;
    return wdc::Wdc5Magic;
  }

  Result<std::uint32_t> TableCore::_freshMagic(std::optional<std::string_view> path) const {
    if (path) {
      if (path->ends_with(".dbc")) return WdbcMagic;
      if (path->ends_with(".db2"))
        return _info.version < builds::Cata
                 ? makeError(ErrorCode::NotSupported,
                              std::format("{}: .db2 does not exist before Cataclysm", _info.name))
                 : Result<std::uint32_t>{_db2MagicForVersion()};
    }
    if (_info.version < builds::Cata) return WdbcMagic;
    return _db2MagicForVersion();
  }

  Result<FileBuffer> TableCore::_writeAs(std::uint32_t magic, EncryptedPolicy policy) const {
    ErasedRecordSource erased{_vec, _ops};
    const RecordSource& source = _extSource ? *_extSource : static_cast<const RecordSource&>(erased);
    if (magic == WdbcMagic) return writeWdbc(_info, source, _state);
    if (magic == Wdb2Magic) return writeWdb2(_info, source, _state);
    if (wdc::isWdcMagic(magic)) return wdc::writeWdc(magic, _info, source, _state, policy);
    return makeError(ErrorCode::NotImplemented, std::format("{}: writing '{}' tables is not implemented yet",
                                                             _info.name, formats::fourccToString(
                                                               magic, formats::FourCCEndian::Forward)));
  }
}
