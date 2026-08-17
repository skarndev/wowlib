#include <wowlib/fs/mpq/mpq_storage.hpp>

#include <fstream>
#include <format>
#include <ranges>
#include <set>
#include <tuple>

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

  // The names StormLib synthesizes rather than stores: the archive metadata
  // pseudo-files, and the "File########.ext" placeholders it invents for
  // hash-table entries whose real name no listfile covers (reads by such a
  // name would miss, so listing them would only manufacture failures).
  bool is_synthetic_name(std::string_view canonical)
  {
    for (const std::string_view metadata :
         {"(listfile)", "(attributes)", "(signature)", "(patch_metadata)"})
      if (canonical == metadata)
        return true;

    if (canonical.size() < 13 || !canonical.starts_with("file") || canonical[12] != '.')
      return false;
    for (std::size_t i = 4; i < 12; ++i)
      if (canonical[i] < '0' || canonical[i] > '9')
        return false;
    return true;
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
        // A wow-update archive holds PTCH deltas and added files; it attaches
        // to every base archive of its own Data directory (updates come after
        // all base members in the chain, so those are open by now). StormLib
        // then serves the patched content transparently through the base
        // handles.
        //
        // The prefix is passed as NULL on purpose — that is what makes
        // patching WORK. StormLib's FindPatchPrefix treats a non-null prefix
        // as an override and prepends it to every lookup in the patch
        // archive; retail WoW updates store their entries under BARE paths
        // ("dbfilesclient\achievement.dbc"), so an explicit "base"/"enUS"
        // makes every lookup miss and the base file is served unpatched, with
        // no error anywhere. NULL selects StormLib's own detection, which
        // recognizes the prefixed Cata-beta archives by their
        // "base\(patch_metadata)" marker and assumes no prefix otherwise.
        // (Found 2026-08-09: the 5.4.8 client appeared to ship 5.0.x
        // databases because 18 locale updates' DBC deltas were all ignored.)
        // StormLib is built ANSI on every platform (TCHAR == char), so paths
        // cross its API boundary as narrow strings — same as the CascLib side.
        const std::string patch_path = member.path.string();
        for (OpenedArchive& archive : _archives)
        {
          if (archive.is_directory
              || archive.path.parent_path() != member.path.parent_path())
            continue;
          if (!SFileOpenPatchArchive(archive.handle, patch_path.c_str(), nullptr, 0))
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
        std::error_code walk_ec;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator{member.path, walk_ec})
        {
          if (!entry.is_regular_file(walk_ec))
            continue;
          const auto relative =
              std::filesystem::relative(entry.path(), member.path, walk_ec);
          if (walk_ec)
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

  Result<std::vector<std::string>> MpqStorage::enumerate_paths()
  {
    if (!is_open())
      return make_error(ErrorCode::StorageNotOpen, "MPQ storage is not open");

    // A std::set both deduplicates across the chain and hands the paths back
    // sorted, matching the contract in one structure.
    std::set<std::string> paths;

    for (const OpenedArchive& archive : _archives)
    {
      if (archive.is_directory)
      {
        // Loose members are indexed by canonical path already.
        for (const auto& name : archive.loose | std::views::keys)
          paths.insert(name);
        continue;
      }

      std::scoped_lock lock{*archive.mtx};

      // Archives open with MPQ_OPEN_NO_LISTFILE (see open_chain), so the name
      // source must be loaded on demand; a nullptr list file means "the
      // archive's own internal listfile" (StormLib walks the patch chain too).
      // Failure is fine — the find below then yields only placeholder names,
      // which are filtered out, effectively skipping the archive.
      std::ignore = SFileAddListFile(archive.handle, nullptr);

      SFILE_FIND_DATA found{};
      HANDLE find = SFileFindFirstFile(archive.handle, "*", &found, nullptr);
      if (!find)
        continue;  // nothing enumerable in this archive — skip it silently
      do
      {
        std::string canonical = normalize_path(found.cFileName);
        if (!is_synthetic_name(canonical))
          paths.insert(std::move(canonical));
      } while (SFileFindNextFile(find, &found));
      SFileFindClose(find);
    }

    return std::vector<std::string>{std::make_move_iterator(paths.begin()),
                                    std::make_move_iterator(paths.end())};
  }
}