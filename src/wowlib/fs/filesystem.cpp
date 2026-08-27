#include <wowlib/fs/filesystem.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

namespace wowlib::fs {
  namespace {
    // The client's archive directory is canonically `Data/`, but repacks exist
    // that ship it lowercase, which matters on case-sensitive filesystems.
    // Prefer the canonical spelling; otherwise take any case variant present;
    // fall back to the canonical path so the storage produces its natural
    // "nothing there" error.
    std::filesystem::path resolveDataDir(const std::filesystem::path& clientPath) {
      std::error_code ec;
      if (std::filesystem::is_directory(clientPath / "Data", ec)) return clientPath / "Data";
      for (const auto& entry : std::filesystem::directory_iterator{clientPath, ec}) {
        const std::string name = entry.path().filename().string();
        if (name.size() == 4 && entry.is_directory(ec) && std::tolower(static_cast<unsigned char>(name[0])) == 'd' &&
          std::tolower(static_cast<unsigned char>(name[1])) == 'a' && std::tolower(static_cast<unsigned char>(name[2]))
          == 't' && std::tolower(static_cast<unsigned char>(name[3])) == 'a') return entry.path();
      }
      return clientPath / "Data";
    }
  }

  Result<FileSystemSettings> FileSystemSettings::detect(std::filesystem::path clientPath,
                                                        Locale locale,
                                                        std::optional<std::filesystem::path> projectDirectory,
                                                        std::optional<std::filesystem::path> listfileCsv,
                                                        FileDataID customFdidStart) {
    auto install = ClientInstall::detect(std::move(clientPath));
    if (!install) return std::unexpected(install.error());

    return FileSystemSettings{
      .clientPath = std::move(install->path),
      .version = install->version,
      .locale = locale,
      .projectDirectory = std::move(projectDirectory),
      .listfileCsv = std::move(listfileCsv),
      .customFdidStart = customFdidStart,
      .cascProduct = std::move(install->cascProduct)
    };
  }

  Result<FileSystem> FileSystem::open(FileSystemSettings settings) {
    std::optional<ProjectDirectory> project;
    if (settings.projectDirectory) {
      auto opened = ProjectDirectory::open(*settings.projectDirectory);
      if (!opened) return std::unexpected(opened.error());
      project = std::move(*opened);
    }

    if (settings.version.storageKind() == StorageKind::Mpq) {
      auto storage = MpqStorage::open({
        .dataDir = resolveDataDir(settings.clientPath),
        .version = settings.version,
        .locale = settings.locale
      });
      if (!storage) return std::unexpected(storage.error());

      return FileSystem{MpqFileSystem{std::move(*storage), NullListfile{}, std::move(project)}, settings.version};
    }

    // The supplied CSV is the working database: lookups read it, registrations
    // append to it. Without one, FDID-only access still works.
    CsvListfile listfile;
    if (settings.listfileCsv) {
      auto loaded = CsvListfile::load(*settings.listfileCsv, {.customFdidStart = settings.customFdidStart});
      if (!loaded) return std::unexpected(loaded.error());
      listfile = std::move(*loaded);
    }

    auto storage = CascStorage::open({
      .clientRoot = settings.clientPath,
      .product = settings.cascProduct.value_or(std::string{settings.version.defaultCascProduct()}),
      .locale = settings.locale,
      .build = settings.version.build
    });
    if (!storage) return std::unexpected(storage.error());

    return FileSystem{CascFileSystem{std::move(*storage), std::move(listfile), std::move(project)}, settings.version};
  }

  namespace {
    // The monostate alternative is the closed state; only close() (scripting
    // languages) can reach it, and every entry point degrades to this error.
    std::unexpected<Error> closedError() {
      return makeError(ErrorCode::StorageNotOpen, "the filesystem is closed");
    }

    template <typename T> concept IsComposition = !std::is_same_v<std::remove_cvref_t<T>, std::monostate>;
  }

  Result<FileBuffer> FileSystem::readFile(const FileKey& key) {
    return std::visit([&](auto& fs) -> Result<FileBuffer> {
      if constexpr (IsComposition<decltype(fs)>) return fs.readFile(key);
      else return closedError();
    }, _impl);
  }

  bool FileSystem::exists(const FileKey& key) {
    return std::visit([&](auto& fs) {
      if constexpr (IsComposition<decltype(fs)>) return fs.exists(key);
      else return false;
    }, _impl);
  }

  Result<std::vector<std::string>> FileSystem::enumeratePaths() {
    if (auto* mpqFs = std::get_if<MpqFileSystem>(&_impl)) return mpqFs->backend().enumeratePaths();

    if (auto* cascFs = std::get_if<CascFileSystem>(&_impl)) {
      // CASC storages are id-addressed; the listing is every id the listfile
      // can name. Unnamed ids are dropped — a path listing is only as
      // complete as the listfile, which is the CASC reality anyway.
      auto fdids = cascFs->backend().enumerateFdids();
      if (!fdids) return std::unexpected(fdids.error());

      std::vector<std::string> paths;
      paths.reserve(fdids->size());
      for (const FileDataID fdid : *fdids)
        if (auto path = cascFs->listfile().fdidToPath(fdid)) paths.push_back(std::move(*path));
      std::ranges::sort(paths);
      const auto duplicates = std::ranges::unique(paths);
      paths.erase(duplicates.begin(), duplicates.end());
      return paths;
    }

    return closedError();
  }

  FileKey FileSystem::resolve(const FileKey& key) const {
    return std::visit([&](const auto& fs) -> FileKey {
      if constexpr (IsComposition<decltype(fs)>) return fs.resolve(key);
      else return key; // closed: nothing to consult, the key passes unchanged
    }, _impl);
  }

  Result<FileDataID> FileSystem::addFile(std::string_view path, std::span<const std::byte> content) {
    return std::visit([&](auto& fs) -> Result<FileDataID> {
      if constexpr (IsComposition<decltype(fs)>) return fs.addFile(path, content);
      else return closedError();
    }, _impl);
  }

  // The concrete compositions; instantiated here so the static lib always
  // carries them for C++ consumers.
  template class ClientFileSystem<MpqStorage, NullListfile>;
  template class ClientFileSystem<CascStorage, CsvListfile>;
}
