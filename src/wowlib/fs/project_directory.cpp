#include <wowlib/fs/project_directory.hpp>

#include <format>
#include <fstream>
#include <mutex>

#include <wowlib/core/path.hpp>

namespace wowlib::fs
{
  namespace
  {
    namespace fsys = std::filesystem;
  }

  ProjectDirectory::ProjectDirectory(ProjectDirectory&& other) noexcept
  {
    std::unique_lock lock{other._mtx};
    _root = std::move(other._root);
    _index = std::move(other._index);
  }

  ProjectDirectory& ProjectDirectory::operator=(ProjectDirectory&& other) noexcept
  {
    if (this != &other)
    {
      std::scoped_lock lock{_mtx, other._mtx};
      _root = std::move(other._root);
      _index = std::move(other._index);
    }
    return *this;
  }

  Result<ProjectDirectory> ProjectDirectory::open(std::filesystem::path root)
  {
    std::error_code ec;
    fsys::create_directories(root, ec);
    if (ec)
      return make_error(ErrorCode::IoError,
                        std::format("cannot create project directory '{}': {}",
                                    root.string(), ec.message()));

    root = fsys::absolute(root, ec);
    if (ec)
      return make_error(ErrorCode::IoError,
                        std::format("cannot resolve project directory '{}': {}",
                                    root.string(), ec.message()));

    ProjectDirectory result;
    result._root = std::move(root);
    result.index_tree_locked();   // sole owner, no readers yet
    return result;
  }

  void ProjectDirectory::index_tree_locked()
  {
    _index.clear();

    std::error_code ec;
    for (fsys::recursive_directory_iterator it{_root, ec}, end; !ec && it != end;
         it.increment(ec))
    {
      if (!it->is_regular_file(ec))
        continue;

      auto relative = fsys::relative(it->path(), _root, ec);
      if (ec)
        continue;

      _index.insert_or_assign(normalize_path(relative.generic_string()), it->path());
    }
  }

  void ProjectDirectory::rescan()
  {
    std::unique_lock lock{_mtx};
    index_tree_locked();
  }

  std::optional<std::filesystem::path> ProjectDirectory::resolve(std::string_view path) const
  {
    const std::string canonical = normalize_path(path);
    std::shared_lock lock{_mtx};
    const auto it = _index.find(canonical);
    return it == _index.end() ? std::nullopt : std::optional{it->second};
  }

  bool ProjectDirectory::exists(std::string_view path) const
  {
    return resolve(path).has_value();
  }

  Result<FileBuffer> ProjectDirectory::read(std::string_view path) const
  {
    const auto on_disk = resolve(path);
    if (!on_disk)
      return make_error(ErrorCode::FileNotFound,
                        std::format("'{}' is not in the project directory",
                                    normalize_path(path)));

    std::ifstream file{*on_disk, std::ios::binary | std::ios::ate};
    if (!file)
      return make_error(ErrorCode::IoError,
                        std::format("cannot open '{}'", on_disk->string()));

    FileBuffer buffer(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!buffer.empty() &&
        !file.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size())))
      return make_error(ErrorCode::IoError,
                        std::format("I/O error reading '{}'", on_disk->string()));

    return buffer;
  }

  Result<void> ProjectDirectory::write(std::string_view path,
                                       std::span<const std::byte> content)
  {
    const std::string canonical = normalize_path(path);
    if (canonical.empty())
      return make_error(ErrorCode::InvalidPath, "cannot write an empty path");

    const fsys::path on_disk = _root / to_native_relative(canonical);

    std::error_code ec;
    fsys::create_directories(on_disk.parent_path(), ec);
    if (ec)
      return make_error(ErrorCode::IoError,
                        std::format("cannot create directories for '{}': {}",
                                    on_disk.string(), ec.message()));

    std::ofstream file{on_disk, std::ios::binary | std::ios::trunc};
    if (!file)
      return make_error(ErrorCode::IoError,
                        std::format("cannot write '{}'", on_disk.string()));

    if (!content.empty())
      file.write(reinterpret_cast<const char*>(content.data()),
                 static_cast<std::streamsize>(content.size()));
    if (!file.flush())
      return make_error(ErrorCode::IoError,
                        std::format("I/O error writing '{}'", on_disk.string()));

    std::unique_lock lock{_mtx};
    _index.insert_or_assign(canonical, on_disk);
    return {};
  }

  std::size_t ProjectDirectory::size() const
  {
    std::shared_lock lock{_mtx};
    return _index.size();
  }
}