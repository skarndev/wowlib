#include <wowlib/fs/csv_listfile.hpp>

#include <charconv>
#include <format>
#include <fstream>
#include <mutex>

#include <wowlib/core/path.hpp>

namespace wowlib
{
  namespace
  {
    // Parses one "fileDataId;filepath" line; empty lines yield nullopt, malformed
    // lines yield an error carrying the line number.
    Result<std::optional<std::pair<std::uint32_t, std::string>>>
    parse_line(std::string_view line, std::size_t line_no)
    {
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      if (line.empty())
        return std::optional<std::pair<std::uint32_t, std::string>>{};

      const auto sep = line.find(';');
      if (sep == std::string_view::npos || sep == 0 || sep + 1 >= line.size())
        return make_error(ErrorCode::ListfileParseError,
                          std::format("listfile line {}: expected 'fileDataId;filepath'",
                                      line_no));

      std::uint32_t id = 0;
      const auto [ptr, ec] = std::from_chars(line.data(), line.data() + sep, id);
      if (ec != std::errc{} || ptr != line.data() + sep)
        return make_error(ErrorCode::ListfileParseError,
                          std::format("listfile line {}: bad fileDataId '{}'",
                                      line_no, line.substr(0, sep)));

      return std::optional{std::pair{id, normalize_path(line.substr(sep + 1))}};
    }
  }

  CsvListfile::CsvListfile(CsvListfile&& other) noexcept
  {
    std::unique_lock lock{other.mtx_};
    path_to_id_ = std::move(other.path_to_id_);
    id_to_path_ = std::move(other.id_to_path_);
    custom_entries_ = std::move(other.custom_entries_);
    allocator_ = other.allocator_;
  }

  CsvListfile& CsvListfile::operator=(CsvListfile&& other) noexcept
  {
    if (this != &other)
    {
      std::scoped_lock lock{mtx_, other.mtx_};
      path_to_id_ = std::move(other.path_to_id_);
      id_to_path_ = std::move(other.id_to_path_);
      custom_entries_ = std::move(other.custom_entries_);
      allocator_ = other.allocator_;
    }
    return *this;
  }

  Result<CsvListfile> CsvListfile::load(const std::filesystem::path& csv, Options options)
  {
    std::ifstream file{csv};
    if (!file)
      return make_error(ErrorCode::ListfileIoError,
                        std::format("cannot open listfile '{}'", csv.string()));

    CsvListfile result;
    result.allocator_ = FdidAllocator{options.custom_fdid_start};

    // The community listfile is ~6M entries; reserving ahead avoids rehash storms.
    result.path_to_id_.reserve(8'000'000);
    result.id_to_path_.reserve(8'000'000);

    std::string line;
    for (std::size_t line_no = 1; std::getline(file, line); ++line_no)
    {
      auto parsed = parse_line(line, line_no);
      if (!parsed)
        return std::unexpected(parsed.error());
      if (!*parsed)
        continue;

      auto& [id, path] = **parsed;
      result.id_to_path_.insert_or_assign(id, path);
      result.path_to_id_.insert_or_assign(std::move(path), FileDataID{id});
    }

    if (file.bad())
      return make_error(ErrorCode::ListfileIoError,
                        std::format("I/O error reading listfile '{}'", csv.string()));

    return result;
  }

  Result<void> CsvListfile::load_custom_entries(const std::filesystem::path& sidecar)
  {
    std::ifstream file{sidecar};
    if (!file)
      return make_error(ErrorCode::ListfileIoError,
                        std::format("cannot open sidecar '{}'", sidecar.string()));

    std::unique_lock lock{mtx_};

    std::string line;
    for (std::size_t line_no = 1; std::getline(file, line); ++line_no)
    {
      auto parsed = parse_line(line, line_no);
      if (!parsed)
        return std::unexpected(parsed.error());
      if (!*parsed)
        continue;

      auto& [id, path] = **parsed;
      custom_entries_.insert_or_assign(id, path);
      id_to_path_.insert_or_assign(id, path);
      path_to_id_.insert_or_assign(std::move(path), FileDataID{id});
      allocator_.note_existing(FileDataID{id});
    }

    if (file.bad())
      return make_error(ErrorCode::ListfileIoError,
                        std::format("I/O error reading sidecar '{}'", sidecar.string()));

    return {};
  }

  Result<void> CsvListfile::save_custom_entries(const std::filesystem::path& sidecar) const
  {
    std::shared_lock lock{mtx_};

    std::error_code ec;
    std::filesystem::create_directories(sidecar.parent_path(), ec);

    std::ofstream file{sidecar, std::ios::trunc};
    if (!file)
      return make_error(ErrorCode::ListfileIoError,
                        std::format("cannot write sidecar '{}'", sidecar.string()));

    for (const auto& [id, path] : custom_entries_)
      file << id << ';' << to_native_relative(path) << '\n';

    if (!file.flush())
      return make_error(ErrorCode::ListfileIoError,
                        std::format("I/O error writing sidecar '{}'", sidecar.string()));

    return {};
  }

  std::optional<FileDataID> CsvListfile::path_to_fdid(std::string_view path) const
  {
    const std::string canonical = normalize_path(path);
    std::shared_lock lock{mtx_};
    const auto it = path_to_id_.find(canonical);
    return it == path_to_id_.end() ? std::nullopt : std::optional{it->second};
  }

  std::optional<std::string> CsvListfile::fdid_to_path(FileDataID fdid) const
  {
    std::shared_lock lock{mtx_};
    const auto it = id_to_path_.find(fdid.value);
    return it == id_to_path_.end() ? std::nullopt : std::optional{it->second};
  }

  Result<FileDataID> CsvListfile::register_path(std::string_view path)
  {
    std::string canonical = normalize_path(path);
    if (canonical.empty())
      return make_error(ErrorCode::InvalidPath, "cannot register an empty path");

    std::unique_lock lock{mtx_};

    if (path_to_id_.contains(canonical))
      return make_error(ErrorCode::DuplicatePath,
                        std::format("path '{}' already has a FileDataID", canonical));

    auto id = allocator_.next();
    if (!id)
      return std::unexpected(id.error());

    custom_entries_.insert_or_assign(id->value, canonical);
    id_to_path_.insert_or_assign(id->value, canonical);
    path_to_id_.insert_or_assign(std::move(canonical), *id);
    return *id;
  }

  bool CsvListfile::contains(std::string_view path) const
  {
    const std::string canonical = normalize_path(path);
    std::shared_lock lock{mtx_};
    return path_to_id_.contains(canonical);
  }

  std::size_t CsvListfile::size() const
  {
    std::shared_lock lock{mtx_};
    return path_to_id_.size();
  }
}