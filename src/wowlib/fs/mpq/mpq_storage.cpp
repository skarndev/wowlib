#include <wowlib/fs/mpq/mpq_storage.hpp>

#include <format>

#define STORMLIB_NO_AUTO_LINK
#include <StormLib.h>

namespace wowlib
{
  namespace
  {
    // Canonical form already uses backslashes — StormLib's separator — so keys
    // pass through unchanged; this is just the null-terminated copy.
    std::string storm_name(const std::string& canonical_path) { return canonical_path; }
  }

  Result<void> MpqStorage::open()
  {
    if (is_open())
      return {};

    const MpqChainSpec* spec = find_chain_spec(options_.version);
    if (!spec)
      return make_error(ErrorCode::StorageOpenFailed,
                        std::format("no MPQ chain table for client {}.{}.{} (build {})",
                                    options_.version.major, options_.version.minor,
                                    options_.version.patch, options_.version.build));

    locale_ = options_.locale ? options_.locale : detect_locale(options_.data_dir);
    if (!locale_)
    {
      const auto found = detect_locales(options_.data_dir);
      return make_error(
        ErrorCode::StorageOpenFailed,
        found.empty()
          ? std::format("no locale directory found under '{}'", options_.data_dir.string())
          : std::format("{} locale directories found under '{}'; pass one explicitly",
                        found.size(), options_.data_dir.string()));
    }

    auto chain = expand_chain(*spec, options_.data_dir, *locale_);
    if (!chain)
      return std::unexpected(chain.error());
    if (chain->empty())
      return make_error(ErrorCode::StorageOpenFailed,
                        std::format("no archives of the {}.{}.{} chain exist under '{}'",
                                    options_.version.major, options_.version.minor,
                                    options_.version.patch, options_.data_dir.string()));

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
      archives_.push_back(OpenedArchive{archive_path, handle,
                                        std::make_unique<std::mutex>()});
    }
    return {};
  }

  void MpqStorage::close() noexcept
  {
    for (auto& archive : archives_)
      if (archive.handle)
        SFileCloseArchive(archive.handle);
    archives_.clear();
  }

  Result<FileBuffer> MpqStorage::read_file(const FileKey& key)
  {
    if (!is_open())
      return make_error(ErrorCode::StorageNotOpen, "MPQ storage is not open");
    if (!key.path)
      return make_error(ErrorCode::FdidNotResolvable,
                        "MPQ storage is path-addressed; resolve the FileDataID to a "
                        "path through a listfile first");

    const std::string name = storm_name(*key.path);

    // Reverse load order: the last archive that carries the file wins.
    for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
    {
      std::scoped_lock lock{*it->mtx};

      if (!SFileHasFile(it->handle, name.c_str()))
        continue;

      HANDLE file = nullptr;
      if (!SFileOpenFileEx(it->handle, name.c_str(), SFILE_OPEN_FROM_MPQ, &file))
        return make_error(ErrorCode::BackendError,
                          std::format("SFileOpenFileEx failed for '{}' in '{}'", name,
                                      it->path.string()),
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

    const std::string name = storm_name(*key.path);
    for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
    {
      std::scoped_lock lock{*it->mtx};
      if (SFileHasFile(it->handle, name.c_str()))
        return true;
    }
    return false;
  }
}