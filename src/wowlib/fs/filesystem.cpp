#include <wowlib/fs/filesystem.hpp>

#include <format>

namespace wowlib
{
  namespace
  {
    constexpr std::string_view sidecar_relative = ".wowlib/custom-listfile.csv";
  }

  Result<FileSystem> FileSystem::open(FileSystemSettings settings)
  {
    std::optional<ProjectDirectory> project;
    std::optional<std::filesystem::path> sidecar;
    if (settings.project_directory)
    {
      auto opened = ProjectDirectory::open(*settings.project_directory);
      if (!opened)
        return std::unexpected(opened.error());
      sidecar = opened->root() / sidecar_relative;
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

    CsvListfile listfile;
    if (settings.listfile_csv)
    {
      auto loaded = CsvListfile::load(*settings.listfile_csv,
                                      {.custom_fdid_start = settings.custom_fdid_start});
      if (!loaded)
        return std::unexpected(loaded.error());
      listfile = std::move(*loaded);
    }
    if (sidecar)
    {
      std::error_code ec;
      if (std::filesystem::is_regular_file(*sidecar, ec))
        if (auto merged = listfile.load_custom_entries(*sidecar); !merged)
          return std::unexpected(merged.error());
    }

    CascStorage storage{{.client_root = settings.client_path,
                         .product = settings.casc_product,
                         .locale = settings.locale.value_or(Locale::enUS),
                         .build = settings.version.build}};
    if (auto opened = storage.open(); !opened)
      return std::unexpected(opened.error());

    FileSystem result{CascFileSystem{std::move(storage), std::move(listfile),
                                     std::move(project)}};
    if (sidecar)
      result.casc()->set_sidecar(std::move(*sidecar));
    return result;
  }

  Result<FileBuffer> FileSystem::read_file(std::string_view path)
  {
    return std::visit([&](auto& fs) { return fs.read_file(FileKey{path}); },
                      impl_);
  }

  Result<FileBuffer> FileSystem::read_file(FileDataID fdid)
  {
    return std::visit([&](auto& fs) { return fs.read_file(FileKey{fdid}); },
                      impl_);
  }

  bool FileSystem::exists(std::string_view path)
  {
    return std::visit([&](auto& fs) { return fs.exists(FileKey{path}); }, impl_);
  }

  bool FileSystem::exists(FileDataID fdid)
  {
    return std::visit([&](auto& fs) { return fs.exists(FileKey{fdid}); }, impl_);
  }

  Result<FileDataID> FileSystem::add_file(std::string_view path,
                                          std::span<const std::byte> content)
  {
    return std::visit([&](auto& fs) { return fs.add_file(path, content); }, impl_);
  }

  StorageKind FileSystem::kind() const
  {
    return std::visit([](const auto& fs) {
      return std::remove_cvref_t<decltype(fs)>::kind();
    }, impl_);
  }

  // The concrete compositions welder binds; instantiated here so the static lib
  // always carries them.
  template class ClientFileSystem<MpqStorage, NullListfile>;
  template class ClientFileSystem<CascStorage, CsvListfile>;
}