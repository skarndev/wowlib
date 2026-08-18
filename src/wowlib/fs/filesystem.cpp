#include <wowlib/fs/filesystem.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

namespace wowlib::fs
{
  namespace
  {
    // The client's archive directory is canonically `Data/`, but repacks exist
    // that ship it lowercase, which matters on case-sensitive filesystems.
    // Prefer the canonical spelling; otherwise take any case variant present;
    // fall back to the canonical path so the storage produces its natural
    // "nothing there" error.
    std::filesystem::path resolve_data_dir(const std::filesystem::path& client_path)
    {
      std::error_code ec;
      if (std::filesystem::is_directory(client_path / "Data", ec))
        return client_path / "Data";
      for (const auto& entry : std::filesystem::directory_iterator{client_path, ec})
      {
        const std::string name = entry.path().filename().string();
        if (name.size() == 4 && entry.is_directory(ec)
            && std::tolower(static_cast<unsigned char>(name[0])) == 'd'
            && std::tolower(static_cast<unsigned char>(name[1])) == 'a'
            && std::tolower(static_cast<unsigned char>(name[2])) == 't'
            && std::tolower(static_cast<unsigned char>(name[3])) == 'a')
          return entry.path();
      }
      return client_path / "Data";
    }
  }

  Result<FileSystemSettings> FileSystemSettings::detect(
    std::filesystem::path client_path, Locale locale,
    std::optional<std::filesystem::path> project_directory,
    std::optional<std::filesystem::path> listfile_csv, FileDataID custom_fdid_start)
  {
    auto install = ClientInstall::detect(std::move(client_path));
    if (!install)
      return std::unexpected(install.error());

    return FileSystemSettings{.client_path = std::move(install->path),
                              .version = install->version,
                              .locale = locale,
                              .project_directory = std::move(project_directory),
                              .listfile_csv = std::move(listfile_csv),
                              .custom_fdid_start = custom_fdid_start,
                              .casc_product = std::move(install->casc_product)};
  }

  Result<FileSystem> FileSystem::open(FileSystemSettings settings)
  {
    std::optional<ProjectDirectory> project;
    if (settings.project_directory)
    {
      auto opened = ProjectDirectory::open(*settings.project_directory);
      if (!opened)
        return std::unexpected(opened.error());
      project = std::move(*opened);
    }

    if (settings.version.storage_kind() == StorageKind::Mpq)
    {
      auto storage = MpqStorage::open({.data_dir = resolve_data_dir(settings.client_path),
                                       .version = settings.version,
                                       .locale = settings.locale});
      if (!storage)
        return std::unexpected(storage.error());

      return FileSystem{MpqFileSystem{std::move(*storage), NullListfile{},
                                      std::move(project)},
                        settings.version};
    }

    // The supplied CSV is the working database: lookups read it, registrations
    // append to it. Without one, FDID-only access still works.
    CsvListfile listfile;
    if (settings.listfile_csv)
    {
      auto loaded = CsvListfile::load(*settings.listfile_csv,
                                      {.custom_fdid_start = settings.custom_fdid_start});
      if (!loaded)
        return std::unexpected(loaded.error());
      listfile = std::move(*loaded);
    }

    auto storage = CascStorage::open({.client_root = settings.client_path,
                                      .product = settings.casc_product.value_or(
                                        std::string{settings.version.default_casc_product()}),
                                      .locale = settings.locale,
                                      .build = settings.version.build});
    if (!storage)
      return std::unexpected(storage.error());

    return FileSystem{CascFileSystem{std::move(*storage), std::move(listfile),
                                     std::move(project)},
                      settings.version};
  }

  namespace
  {
    // The monostate alternative is the closed state; only close() (scripting
    // languages) can reach it, and every entry point degrades to this error.
    std::unexpected<Error> closed_error()
    {
      return make_error(ErrorCode::StorageNotOpen, "the filesystem is closed");
    }

    template <typename T>
    concept IsComposition = !std::is_same_v<std::remove_cvref_t<T>, std::monostate>;
  }

  Result<FileBuffer> FileSystem::read_file(const FileKey& key)
  {
    return std::visit([&](auto& fs) -> Result<FileBuffer> {
      if constexpr (IsComposition<decltype(fs)>)
        return fs.read_file(key);
      else
        return closed_error();
    }, _impl);
  }

  bool FileSystem::exists(const FileKey& key)
  {
    return std::visit([&](auto& fs) {
      if constexpr (IsComposition<decltype(fs)>)
        return fs.exists(key);
      else
        return false;
    }, _impl);
  }

  Result<std::vector<std::string>> FileSystem::enumerate_paths()
  {
    if (auto* mpq_fs = std::get_if<MpqFileSystem>(&_impl))
      return mpq_fs->backend().enumerate_paths();

    if (auto* casc_fs = std::get_if<CascFileSystem>(&_impl))
    {
      // CASC storages are id-addressed; the listing is every id the listfile
      // can name. Unnamed ids are dropped — a path listing is only as
      // complete as the listfile, which is the CASC reality anyway.
      auto fdids = casc_fs->backend().enumerate_fdids();
      if (!fdids)
        return std::unexpected(fdids.error());

      std::vector<std::string> paths;
      paths.reserve(fdids->size());
      for (const FileDataID fdid : *fdids)
        if (auto path = casc_fs->listfile().fdid_to_path(fdid))
          paths.push_back(std::move(*path));
      std::ranges::sort(paths);
      const auto duplicates = std::ranges::unique(paths);
      paths.erase(duplicates.begin(), duplicates.end());
      return paths;
    }

    return closed_error();
  }

  FileKey FileSystem::resolve(const FileKey& key) const
  {
    return std::visit([&](const auto& fs) -> FileKey {
      if constexpr (IsComposition<decltype(fs)>)
        return fs.resolve(key);
      else
        return key;   // closed: nothing to consult, the key passes unchanged
    }, _impl);
  }

  Result<FileDataID> FileSystem::add_file(std::string_view path,
                                          std::span<const std::byte> content)
  {
    return std::visit([&](auto& fs) -> Result<FileDataID> {
      if constexpr (IsComposition<decltype(fs)>)
        return fs.add_file(path, content);
      else
        return closed_error();
    }, _impl);
  }

  // The concrete compositions; instantiated here so the static lib always
  // carries them for C++ consumers.
  template class ClientFileSystem<MpqStorage, NullListfile>;
  template class ClientFileSystem<CascStorage, CsvListfile>;
}