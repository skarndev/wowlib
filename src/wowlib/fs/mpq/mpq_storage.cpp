#include <wowlib/fs/mpq/mpq_storage.hpp>

#include <format>
#include <ranges>

#define STORMLIB_NO_AUTO_LINK
#include <StormLib.h>

namespace wowlib::fs
{
  Result<void> MpqStorage::open()
  {
    if (is_open())
      return {};

    const detail::MpqChainSpec* spec = detail::find_chain_spec(_options.version);
    if (!spec)
      return make_error(ErrorCode::StorageOpenFailed,
                        std::format("no MPQ chain table for client {}.{}.{} (build {})",
                                    _options.version.major, _options.version.minor,
                                    _options.version.patch, _options.version.build));

    _locale = _options.locale ? _options.locale : detail::detect_locale(_options.data_dir);
    if (!_locale)
    {
      const auto found = detail::detect_locales(_options.data_dir);
      return make_error(
        ErrorCode::StorageOpenFailed,
        found.empty()
          ? std::format("no locale directory found under '{}'", _options.data_dir.string())
          : std::format("{} locale directories found under '{}'; pass one explicitly",
                        found.size(), _options.data_dir.string()));
    }

    auto chain = detail::expand_chain(*spec, _options.data_dir, *_locale);
    if (!chain)
      return std::unexpected(chain.error());
    if (chain->empty())
      return make_error(ErrorCode::StorageOpenFailed,
                        std::format("no archives of the {}.{}.{} chain exist under '{}'",
                                    _options.version.major, _options.version.minor,
                                    _options.version.patch, _options.data_dir.string()));

    for (const auto& archive_path : *chain)
    {
      // NO_LISTFILE/NO_ATTRIBUTES: exact-path reads resolve through the hash
      // table alone, and parsing those internal files costs seconds per large
      // archive (7s for 3.3.5a common.MPQ vs 10ms without). Features that need
      // enumeration must load the listfile on demand instead.
      constexpr DWORD open_flags = MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE |
                                   MPQ_OPEN_NO_ATTRIBUTES | MPQ_OPEN_NO_HEADER_SEARCH;

      HANDLE handle = nullptr;
      if (!SFileOpenArchive(archive_path.c_str(), 0, open_flags, &handle))
      {
        const auto native = SErrGetLastError();
        close();
        return make_error(ErrorCode::ArchiveOpenFailed,
                          std::format("SFileOpenArchive failed for '{}'",
                                      archive_path.string()),
                          static_cast<std::uint32_t>(native));
      }
      _archives.push_back(OpenedArchive{archive_path, handle,
                                        std::make_unique<std::mutex>()});
    }
    return {};
  }

  void MpqStorage::close() noexcept
  {
    for (auto& archive : _archives)
      if (archive.handle)
        SFileCloseArchive(archive.handle);
    _archives.clear();
  }

  Result<FileBuffer> MpqStorage::read_file(const FileKey& key)
  {
    if (!is_open())
      return make_error(ErrorCode::StorageNotOpen, "MPQ storage is not open");
    if (!key.path)
      return make_error(ErrorCode::FdidNotResolvable,
                        "MPQ storage is path-addressed; resolve the FileDataID to a "
                        "path through a listfile first");

    // Canonical form already uses backslashes — StormLib's separator.
    const std::string& name = *key.path;

    // Reverse load order: the last archive that carries the file wins.
    for (const OpenedArchive& archive : _archives | std::views::reverse)
    {
      std::scoped_lock lock{*archive.mtx};

      if (!SFileHasFile(archive.handle, name.c_str()))
        continue;

      HANDLE file = nullptr;
      if (!SFileOpenFileEx(archive.handle, name.c_str(), SFILE_OPEN_FROM_MPQ, &file))
        return make_error(ErrorCode::BackendError,
                          std::format("SFileOpenFileEx failed for '{}' in '{}'", name,
                                      archive.path.string()),
                          static_cast<std::uint32_t>(SErrGetLastError()));

      DWORD size_high = 0;
      const DWORD size_low = SFileGetFileSize(file, &size_high);
      if (size_low == SFILE_INVALID_SIZE)
      {
        const auto native = SErrGetLastError();
        SFileCloseFile(file);
        return make_error(ErrorCode::BackendError,
                          std::format("SFileGetFileSize failed for '{}'", name),
                          static_cast<std::uint32_t>(native));
      }

      FileBuffer buffer((static_cast<std::uint64_t>(size_high) << 32) | size_low);
      DWORD read = 0;
      if (!buffer.empty() &&
          !SFileReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                         nullptr))
      {
        const auto native = SErrGetLastError();
        SFileCloseFile(file);
        return make_error(ErrorCode::BackendError,
                          std::format("SFileReadFile failed for '{}'", name),
                          static_cast<std::uint32_t>(native));
      }

      SFileCloseFile(file);
      return buffer;
    }

    return make_error(ErrorCode::FileNotFound,
                      std::format("'{}' was not found in the MPQ chain", name));
  }

  bool MpqStorage::exists(const FileKey& key)
  {
    if (!is_open() || !key.path)
      return false;

    for (const OpenedArchive& archive : _archives | std::views::reverse)
    {
      std::scoped_lock lock{*archive.mtx};
      if (SFileHasFile(archive.handle, key.path->c_str()))
        return true;
    }
    return false;
  }
}