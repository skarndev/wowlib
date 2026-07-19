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
      auto storage = MpqStorage::open({.data_dir = settings.client_path / "Data",
                                       .version = settings.version,
                                       .locale = settings.locale});
      if (!storage)
        return std::unexpected(storage.error());

      return FileSystem{MpqFileSystem{std::move(*storage), NullListfile{},
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

    auto storage = CascStorage::open({.client_root = settings.client_path,
                                      .product = settings.casc_product,
                                      .locale = settings.locale,
                                      .build = settings.version.build});
    if (!storage)
      return std::unexpected(storage.error());

    return FileSystem{CascFileSystem{std::move(*storage), std::move(listfile),
                                     std::move(project)}};
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