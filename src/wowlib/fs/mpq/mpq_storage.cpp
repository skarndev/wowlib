#include <wowlib/fs/mpq/mpq_storage.hpp>

#include <fstream>
#include <format>
#include <ranges>

#include <wowlib/core/path.hpp>

#define STORMLIB_NO_AUTO_LINK
#include <StormLib.h>

namespace
{
  namespace fsys = std::filesystem;

  // Read a whole loose file into a buffer. Loose members carry no StormLib state,
  // so no lock is needed.
  wowlib::Result<wowlib::FileBuffer> read_loose_file(const fsys::path& path)
  {
    std::ifstream in{path, std::ios::binary | std::ios::ate};
    if (!in)
      return wowlib::make_error(wowlib::ErrorCode::BackendError,
                                std::format("failed to open loose file '{}'",
                                            path.string()));
    const std::streamoff size = in.tellg();
    if (size < 0)
      return wowlib::make_error(wowlib::ErrorCode::BackendError,
                                std::format("failed to size loose file '{}'",
                                            path.string()));
    wowlib::FileBuffer buffer(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!buffer.empty() &&
        !in.read(reinterpret_cast<char*>(buffer.data()), size))
      return wowlib::make_error(wowlib::ErrorCode::BackendError,
                                std::format("failed to read loose file '{}'",
                                            path.string()));
    return buffer;
  }
}

namespace wowlib::fs
{
  Result<MpqStorage> MpqStorage::open(Options options)
  {
    MpqStorage storage{std::move(options)};
    if (auto opened = storage.open_chain(); !opened)
      return std::unexpected(opened.error());
    return storage;
  }

  Result<void> MpqStorage::open_chain()
  {
    const detail::MpqChainSpec* spec = detail::find_chain_spec(_options.version);
    if (!spec)
      return make_error(ErrorCode::StorageOpenFailed,
                        std::format("no MPQ chain table for client {}.{}.{} (build {})",
                                    _options.version.major, _options.version.minor,
                                    _options.version.patch, _options.version.build));

    // The caller supplies the locale (via FileSystemSettings); we only verify its
    // Data/{code}/ directory is actually present rather than scanning for one.
    // Pre-TBC clients are exempt: stock vanilla installs are flat (locale
    // subdirectories entered the layout with TBC), and only some later repacks
    // retrofit a Data/{code}/ tier — expand_chain mounts it when it exists.
    const std::string code{locale_code(_options.locale)};
    std::error_code ec;
    if (_options.version.major >= 2
        && !std::filesystem::is_directory(_options.data_dir / code, ec))
      return make_error(
        ErrorCode::StorageOpenFailed,
        std::format("locale directory '{}' not found under '{}'", code,
                    _options.data_dir.string()));

    auto chain = detail::expand_chain(*spec, _options.data_dir, _options.locale);
    if (!chain)
      return std::unexpected(chain.error());
    if (chain->empty())
      return make_error(ErrorCode::StorageOpenFailed,
                        std::format("no archives of the {}.{}.{} chain exist under '{}'",
                                    _options.version.major, _options.version.minor,
                                    _options.version.patch, _options.data_dir.string()));

    for (const detail::ChainMember& member : *chain)
    {
      if (member.incremental)
      {
        // A wow-update archive holds PTCH deltas and added files under its
        // path prefix; it attaches to every base archive of its own Data
        // directory (updates come after all base members in the chain, so
        // those are open by now). StormLib then serves the patched content
        // transparently through the base handles.
        // StormLib is built ANSI on every platform (TCHAR == char), so paths
        // cross its API boundary as narrow strings — same as the CascLib side.
        const std::string patch_path = member.path.string();
        for (OpenedArchive& archive : _archives)
        {
          if (archive.is_directory
              || archive.path.parent_path() != member.path.parent_path())
            continue;
          if (!SFileOpenPatchArchive(archive.handle, patch_path.c_str(),
                                     member.prefix.c_str(), 0))
          {
            const auto native = SErrGetLastError();
            close();
            return make_error(
              ErrorCode::ArchiveOpenFailed,
              std::format("SFileOpenPatchArchive failed attaching '{}' to '{}'",
                          member.path.string(), archive.path.string()),
              static_cast<std::uint32_t>(native));
          }
          archive.patched = true;
        }
        continue;
      }

      if (member.is_directory)
      {
        // Loose directory: index every file by its canonical in-game path so
        // reads match the client's case-insensitive lookup (both sides are
        // lowercased/backslashed by normalize_path).
        OpenedArchive slot{member.path, true};
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator{member.path, ec})
        {
          if (!entry.is_regular_file(ec))
            continue;
          const auto relative = std::filesystem::relative(entry.path(), member.path, ec);
          if (ec)
            continue;
          slot.loose.emplace(normalize_path(relative.generic_string()), entry.path());
        }
        _archives.push_back(std::move(slot));
        continue;
      }

      // NO_LISTFILE/NO_ATTRIBUTES: exact-path reads resolve through the hash
      // table alone, and parsing those internal files costs seconds per large
      // archive (7s for 3.3.5a common.MPQ vs 10ms without). Features that need
      // enumeration must load the listfile on demand instead.
      constexpr DWORD open_flags = MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE |
                                   MPQ_OPEN_NO_ATTRIBUTES | MPQ_OPEN_NO_HEADER_SEARCH;

      HANDLE handle = nullptr;
      const std::string archive_path = member.path.string();
      if (!SFileOpenArchive(archive_path.c_str(), 0, open_flags, &handle))
      {
        const auto native = SErrGetLastError();
        close();
        return make_error(ErrorCode::ArchiveOpenFailed,
                          std::format("SFileOpenArchive failed for '{}'",
                                      member.path.string()),
                          static_cast<std::uint32_t>(native));
      }
      _archives.push_back(OpenedArchive{.path = member.path,
                                        .handle = handle,
                                        .mtx = std::make_unique<std::mutex>()});
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

    // Reverse load order: the last member that carries the file wins.
    for (const OpenedArchive& archive : _archives | std::views::reverse)
    {
      if (archive.is_directory)
      {
        const auto it = archive.loose.find(name);
        if (it == archive.loose.end())
          continue;
        return read_loose_file(it->second);
      }

      std::scoped_lock lock{*archive.mtx};

      // The has-file probe checks the archive's own hash table only — it
      // cannot see files ADDED by attached wow-update patches, so patched
      // archives go straight to the open call and treat not-found as a miss.
      if (!archive.patched && !SFileHasFile(archive.handle, name.c_str()))
        continue;

      HANDLE file = nullptr;
      if (!SFileOpenFileEx(archive.handle, name.c_str(), SFILE_OPEN_FROM_MPQ, &file))
      {
        const auto native = SErrGetLastError();
        if (archive.patched && native == ERROR_FILE_NOT_FOUND)
          continue;
        return make_error(ErrorCode::BackendError,
                          std::format("SFileOpenFileEx failed for '{}' in '{}'", name,
                                      archive.path.string()),
                          static_cast<std::uint32_t>(native));
      }

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
      if (archive.is_directory)
      {
        if (archive.loose.contains(*key.path))
          return true;
        continue;
      }

      std::scoped_lock lock{*archive.mtx};
      if (archive.patched)
      {
        // Probe through the open call so patch-added files count (see
        // read_file for why the has-file check is blind to them).
        HANDLE file = nullptr;
        if (SFileOpenFileEx(archive.handle, key.path->c_str(), SFILE_OPEN_FROM_MPQ, &file))
        {
          SFileCloseFile(file);
          return true;
        }
        continue;
      }
      if (SFileHasFile(archive.handle, key.path->c_str()))
        return true;
    }
    return false;
  }
}