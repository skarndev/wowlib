#include <wowlib/fs/filesystem.hpp>

#include <format>

namespace wowlib::fs
{
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
      MpqStorage storage{{.data_dir = settings.client_path / "Data",
                          .version = settings.version,
                          .locale = settings.locale}};
      if (auto opened = storage.open(); !opened)
        return std::unexpected(opened.error());

      return FileSystem{MpqFileSystem{std::move(storage), NullListfile{},
                                      std::move(project)}};
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

    CascStorage storage{{.client_root = settings.client_path,
                         .product = settings.casc_product,
                         .locale = settings.locale.value_or(Locale::enUS),
                         .build = settings.version.build}};
    if (auto opened = storage.open(); !opened)
      return std::unexpected(opened.error());

    return FileSystem{CascFileSystem{std::move(storage), std::move(listfile),
                                     std::move(project)}};
  }

  Result<FileBuffer> FileSystem::read_file(std::string_view path)
  {
    return std::visit([&](auto& fs) { return fs.read_file(FileKey{path}); }, _impl);
  }

  Result<FileBuffer> FileSystem::read_file(FileDataID fdid)
  {
    return std::visit([&](auto& fs) { return fs.read_file(FileKey{fdid}); }, _impl);
  }

  bool FileSystem::exists(std::string_view path)
  {
    return std::visit([&](auto& fs) { return fs.exists(FileKey{path}); }, _impl);
  }

  bool FileSystem::exists(FileDataID fdid)
  {
    return std::visit([&](auto& fs) { return fs.exists(FileKey{fdid}); }, _impl);
  }

  Result<FileDataID> FileSystem::add_file(std::string_view path,
                                          std::span<const std::byte> content)
  {
    return std::visit([&](auto& fs) { return fs.add_file(path, content); }, _impl);
  }

  StorageKind FileSystem::kind() const
  {
    return std::visit([](const auto& fs) {
      return std::remove_cvref_t<decltype(fs)>::kind();
    }, _impl);
  }

  // The concrete compositions; instantiated here so the static lib always
  // carries them for C++ consumers.
  template class ClientFileSystem<MpqStorage, NullListfile>;
  template class ClientFileSystem<CascStorage, CsvListfile>;
}