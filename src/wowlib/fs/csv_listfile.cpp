#include <wowlib/fs/csv_listfile.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <mutex>
#include <vector>

#include <wowlib/core/path.hpp>

namespace wowlib::fs {
  namespace {
    // Parses one "fileDataId;filepath" line; empty lines yield nullopt, malformed
    // lines yield an error carrying the line number.
    Result<std::optional<std::pair<std::uint32_t, std::string>>>
    parseLine(std::string_view line, std::size_t lineNo) {
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      if (line.empty()) return std::optional<std::pair<std::uint32_t, std::string>>{};

      const auto sep = line.find(';');
      if (sep == std::string_view::npos || sep == 0 || sep + 1 >= line.size())
        return makeError(ErrorCode::ListfileParseError,
                          std::format("listfile line {}: expected 'fileDataId;filepath'", lineNo));

      std::uint32_t id = 0;
      const auto [ptr, ec] = std::from_chars(line.data(), line.data() + sep, id);
      if (ec != std::errc{} || ptr != line.data() + sep)
        return makeError(ErrorCode::ListfileParseError,
                          std::format("listfile line {}: bad fileDataId '{}'", lineNo, line.substr(0, sep)));

      return std::optional{std::pair{id, normalizePath(line.substr(sep + 1))}};
    }
  }

  CsvListfile::CsvListfile(CsvListfile&& other) noexcept {
    std::unique_lock lock{other._mtx};
    _source = std::move(other._source);
    _pathToId = std::move(other._pathToId);
    _idToPath = std::move(other._idToPath);
    _allocator = other._allocator;
  }

  CsvListfile& CsvListfile::operator=(CsvListfile&& other) noexcept {
    if (this != &other) {
      std::scoped_lock lock{_mtx, other._mtx};
      _source = std::move(other._source);
      _pathToId = std::move(other._pathToId);
      _idToPath = std::move(other._idToPath);
      _allocator = other._allocator;
    }
    return *this;
  }

  Result<CsvListfile> CsvListfile::load(const std::filesystem::path& csv, Options options) {
    std::ifstream file{csv};
    if (!file)
      return makeError(ErrorCode::ListfileIoError, std::format("cannot open listfile '{}'", csv.string()));

    CsvListfile result;
    result._source = csv;
    result._allocator = detail::FdidAllocator{options.customFdidStart};

    // The community listfile is ~2M entries and growing; reserving ahead avoids
    // rehash storms.
    result._pathToId.reserve(4'000'000);
    result._idToPath.reserve(4'000'000);

    std::string line;
    for (std::size_t lineNo = 1; std::getline(file, line); ++lineNo) {
      auto parsed = parseLine(line, lineNo);
      if (!parsed) return std::unexpected(parsed.error());
      if (!*parsed) continue;

      auto& [id, path] = **parsed;
      result._idToPath.insert_or_assign(id, path);
      result._pathToId.insert_or_assign(std::move(path), FileDataID{id});

      // customizations from a previous session move the cursor past themselves
      result._allocator.noteExisting(FileDataID{id});
    }

    if (file.bad())
      return makeError(ErrorCode::ListfileIoError, std::format("I/O error reading listfile '{}'", csv.string()));

    return result;
  }

  std::optional<FileDataID> CsvListfile::pathToFdid(std::string_view path) const {
    const std::string canonical = normalizePath(path);
    std::shared_lock lock{_mtx};
    const auto it = _pathToId.find(canonical);
    return it == _pathToId.end() ? std::nullopt : std::optional{it->second};
  }

  std::optional<std::string> CsvListfile::fdidToPath(FileDataID fdid) const {
    std::shared_lock lock{_mtx};
    const auto it = _idToPath.find(fdid.value);
    return it == _idToPath.end() ? std::nullopt : std::optional{it->second};
  }

  Result<FileDataID> CsvListfile::registerPath(std::string_view path) {
    std::string canonical = normalizePath(path);
    if (canonical.empty())
      return makeError(ErrorCode::InvalidPath, "cannot register an empty path");

    std::unique_lock lock{_mtx};

    if (_pathToId.contains(canonical))
      return makeError(ErrorCode::DuplicatePath, std::format("path '{}' already has a FileDataID", canonical));

    auto id = _allocator.next();
    if (!id) return std::unexpected(id.error());

    // persist first: on failure the in-memory maps stay untouched (the allocated
    // id is skipped — gaps in the custom range are harmless)
    if (!_source.empty()) {
      // a user-supplied CSV may lack a trailing newline; appending must not glue
      // onto its last line
      bool needsNewline = false;
      if (std::ifstream tail{_source, std::ios::binary | std::ios::ate}; tail)
        if (const auto size = tail.tellg(); size > 0) {
          char last = '\n';
          tail.seekg(-1, std::ios::end);
          tail.get(last);
          needsNewline = last != '\n';
        }

      std::ofstream file{_source, std::ios::app};
      if (needsNewline) file << '\n';
      if (!file || !(file << id->value << ';' << toNativeRelative(canonical) << '\n').flush())
        return makeError(ErrorCode::ListfileIoError, std::format("cannot append to listfile '{}'", _source.string()));
    }

    _idToPath.insert_or_assign(id->value, canonical);
    _pathToId.insert_or_assign(std::move(canonical), *id);
    return *id;
  }

  bool CsvListfile::contains(std::string_view path) const {
    const std::string canonical = normalizePath(path);
    std::shared_lock lock{_mtx};
    return _pathToId.contains(canonical);
  }

  Result<void> CsvListfile::save() const {
    std::shared_lock lock{_mtx};

    if (_source.empty())
      return makeError(ErrorCode::ListfileIoError, "this listfile is in-memory only; nothing to save to");

    std::vector<std::pair<std::uint32_t, const std::string*>> entries;
    entries.reserve(_idToPath.size());
    for (const auto& [id, path] : _idToPath) entries.emplace_back(id, &path);
    std::ranges::sort(entries, {}, &std::pair<std::uint32_t, const std::string*>::first);

    std::ofstream file{_source, std::ios::trunc};
    if (!file)
      return makeError(ErrorCode::ListfileIoError, std::format("cannot write listfile '{}'", _source.string()));

    for (const auto& [id, path] : entries) file << id << ';' << toNativeRelative(*path) << '\n';

    if (!file.flush())
      return makeError(ErrorCode::ListfileIoError, std::format("I/O error writing listfile '{}'", _source.string()));

    return {};
  }

  std::size_t CsvListfile::size() const {
    std::shared_lock lock{_mtx};
    return _pathToId.size();
  }
}
