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
    parse_line(std::string_view line, std::size_t line_no) {
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      if (line.empty()) return std::optional<std::pair<std::uint32_t, std::string>>{};

      const auto sep = line.find(';');
      if (sep == std::string_view::npos || sep == 0 || sep + 1 >= line.size())
        return make_error(ErrorCode::ListfileParseError,
                          std::format("listfile line {}: expected 'fileDataId;filepath'", line_no));

      std::uint32_t id = 0;
      const auto [ptr, ec] = std::from_chars(line.data(), line.data() + sep, id);
      if (ec != std::errc{} || ptr != line.data() + sep)
        return make_error(ErrorCode::ListfileParseError,
                          std::format("listfile line {}: bad fileDataId '{}'", line_no, line.substr(0, sep)));

      return std::optional{std::pair{id, normalize_path(line.substr(sep + 1))}};
    }
  }

  CsvListfile::CsvListfile(CsvListfile&& other) noexcept {
    std::unique_lock lock{other._mtx};
    _source = std::move(other._source);
    _path_to_id = std::move(other._path_to_id);
    _id_to_path = std::move(other._id_to_path);
    _allocator = other._allocator;
  }

  CsvListfile& CsvListfile::operator=(CsvListfile&& other) noexcept {
    if (this != &other) {
      std::scoped_lock lock{_mtx, other._mtx};
      _source = std::move(other._source);
      _path_to_id = std::move(other._path_to_id);
      _id_to_path = std::move(other._id_to_path);
      _allocator = other._allocator;
    }
    return *this;
  }

  Result<CsvListfile> CsvListfile::load(const std::filesystem::path& csv, Options options) {
    std::ifstream file{csv};
    if (!file)
      return make_error(ErrorCode::ListfileIoError, std::format("cannot open listfile '{}'", csv.string()));

    CsvListfile result;
    result._source = csv;
    result._allocator = detail::FdidAllocator{options.custom_fdid_start};

    // The community listfile is ~2M entries and growing; reserving ahead avoids
    // rehash storms.
    result._path_to_id.reserve(4'000'000);
    result._id_to_path.reserve(4'000'000);

    std::string line;
    for (std::size_t line_no = 1; std::getline(file, line); ++line_no) {
      auto parsed = parse_line(line, line_no);
      if (!parsed) return std::unexpected(parsed.error());
      if (!*parsed) continue;

      auto& [id, path] = **parsed;
      result._id_to_path.insert_or_assign(id, path);
      result._path_to_id.insert_or_assign(std::move(path), FileDataID{id});

      // customizations from a previous session move the cursor past themselves
      result._allocator.note_existing(FileDataID{id});
    }

    if (file.bad())
      return make_error(ErrorCode::ListfileIoError, std::format("I/O error reading listfile '{}'", csv.string()));

    return result;
  }

  std::optional<FileDataID> CsvListfile::path_to_fdid(std::string_view path) const {
    const std::string canonical = normalize_path(path);
    std::shared_lock lock{_mtx};
    const auto it = _path_to_id.find(canonical);
    return it == _path_to_id.end() ? std::nullopt : std::optional{it->second};
  }

  std::optional<std::string> CsvListfile::fdid_to_path(FileDataID fdid) const {
    std::shared_lock lock{_mtx};
    const auto it = _id_to_path.find(fdid.value);
    return it == _id_to_path.end() ? std::nullopt : std::optional{it->second};
  }

  Result<FileDataID> CsvListfile::register_path(std::string_view path) {
    std::string canonical = normalize_path(path);
    if (canonical.empty())
      return make_error(ErrorCode::InvalidPath, "cannot register an empty path");

    std::unique_lock lock{_mtx};

    if (_path_to_id.contains(canonical))
      return make_error(ErrorCode::DuplicatePath, std::format("path '{}' already has a FileDataID", canonical));

    auto id = _allocator.next();
    if (!id) return std::unexpected(id.error());

    // persist first: on failure the in-memory maps stay untouched (the allocated
    // id is skipped — gaps in the custom range are harmless)
    if (!_source.empty()) {
      // a user-supplied CSV may lack a trailing newline; appending must not glue
      // onto its last line
      bool needs_newline = false;
      if (std::ifstream tail{_source, std::ios::binary | std::ios::ate}; tail)
        if (const auto size = tail.tellg(); size > 0) {
          char last = '\n';
          tail.seekg(-1, std::ios::end);
          tail.get(last);
          needs_newline = last != '\n';
        }

      std::ofstream file{_source, std::ios::app};
      if (needs_newline) file << '\n';
      if (!file || !(file << id->value << ';' << to_native_relative(canonical) << '\n').flush())
        return make_error(ErrorCode::ListfileIoError, std::format("cannot append to listfile '{}'", _source.string()));
    }

    _id_to_path.insert_or_assign(id->value, canonical);
    _path_to_id.insert_or_assign(std::move(canonical), *id);
    return *id;
  }

  bool CsvListfile::contains(std::string_view path) const {
    const std::string canonical = normalize_path(path);
    std::shared_lock lock{_mtx};
    return _path_to_id.contains(canonical);
  }

  Result<void> CsvListfile::save() const {
    std::shared_lock lock{_mtx};

    if (_source.empty())
      return make_error(ErrorCode::ListfileIoError, "this listfile is in-memory only; nothing to save to");

    std::vector<std::pair<std::uint32_t, const std::string*>> entries;
    entries.reserve(_id_to_path.size());
    for (const auto& [id, path] : _id_to_path) entries.emplace_back(id, &path);
    std::ranges::sort(entries, {}, &std::pair<std::uint32_t, const std::string*>::first);

    std::ofstream file{_source, std::ios::trunc};
    if (!file)
      return make_error(ErrorCode::ListfileIoError, std::format("cannot write listfile '{}'", _source.string()));

    for (const auto& [id, path] : entries) file << id << ';' << to_native_relative(*path) << '\n';

    if (!file.flush())
      return make_error(ErrorCode::ListfileIoError, std::format("I/O error writing listfile '{}'", _source.string()));

    return {};
  }

  std::size_t CsvListfile::size() const {
    std::shared_lock lock{_mtx};
    return _path_to_id.size();
  }
}
