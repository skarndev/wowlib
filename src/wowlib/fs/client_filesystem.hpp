#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/listfile.hpp>
#include <wowlib/fs/project_directory.hpp>
#include <wowlib/fs/storage_backend.hpp>

namespace wowlib
{
  template <StorageBackend Backend, ListfileProvider Listfile = NullListfile>
  class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::policy::opt_in,
    =welder::tparam("Backend", "the storage backend (MpqStorage or CascStorage)"),
    =welder::tparam("Listfile", "the path<->FileDataID database provider"),
    =welder::doc("The static composition of one client's file access: storage backend "
                 "+ listfile database + optional project-directory overlay. Lookup "
                 "precedence: project directory first (the ultimate patch), then the "
                 "client storage. Bound to Python/Lua through its concrete "
                 "instantiations (MpqFileSystem, CascFileSystem).")
  ]]
  ClientFileSystem
  {
  public:
    // excluded from bindings: backends/providers are C++-only types, so target
    // languages construct through FileSystem.open instead
    [[=welder::mark::exclude]]
    ClientFileSystem(Backend backend, Listfile listfile = {},
                     std::optional<ProjectDirectory> project = std::nullopt)
      : backend_(std::move(backend))
      , listfile_(std::move(listfile))
      , project_(std::move(project))
    {
    }

    [[=welder::mark::include,
      =welder::doc("Read a file by key. Missing identity halves are filled from the "
                   "listfile; the project directory wins over the client storage."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> read_file(
      [[=welder::doc("the file identity (path, id, or both)")]] const FileKey& key)
    {
      const FileKey resolved = resolve(key);

      if (project_ && resolved.path && project_->exists(*resolved.path))
        return project_->read(*resolved.path);

      return backend_.read_file(resolved);
    }

    [[=welder::mark::include,
      =welder::doc("Whether a file is reachable in the overlay or the storage."),
      =welder::returns("true if a read would find it")]]
    bool exists([[=welder::doc("the file identity")]] const FileKey& key)
    {
      const FileKey resolved = resolve(key);
      if (project_ && resolved.path && project_->exists(*resolved.path))
        return true;
      return backend_.exists(resolved);
    }

    [[=welder::mark::include,
      =welder::doc("Fill the missing half of a key (path or FileDataID) from the "
                   "listfile, best-effort."),
      =welder::returns("the completed key (unchanged parts preserved)")]]
    FileKey resolve([[=welder::doc("the file identity")]] const FileKey& key) const
    {
      FileKey out = key;
      if (!out.fdid && out.path)
        out.fdid = listfile_.path_to_fdid(*out.path);
      else if (!out.path && out.fdid)
        out.path = listfile_.fdid_to_path(*out.fdid);
      return out;
    }

    [[=welder::mark::include,
      =welder::doc("Add (or overwrite) a file in the project directory. On a "
                   "CASC-era client a new path is also registered in the listfile — "
                   "allocating a custom FileDataID — and the sidecar is persisted."),
      =welder::returns("the file's FileDataID (0 on MPQ-era clients, which have no "
                       "id space)")]]
    Result<FileDataID> add_file(
      [[=welder::doc("the client-internal path of the file")]] std::string_view path,
      [[=welder::doc("the file contents")]] std::span<const std::byte> content)
    {
      if (!project_)
        return make_error(ErrorCode::NotSupported,
                          "no project directory is set; there is nowhere to add files");

      if (auto written = project_->write(path, content); !written)
        return std::unexpected(written.error());

      if constexpr (Backend::kind() == StorageKind::Mpq)
        return FileDataID{0};
      else
      {
        // Overwriting an already-known file keeps its id; only new paths allocate.
        if (auto existing = listfile_.path_to_fdid(path))
          return *existing;

        auto id = listfile_.register_path(path);
        if (id && sidecar_)
          if (auto saved = save_sidecar(); !saved)
            return std::unexpected(saved.error());
        return id;
      }
    }

    /** Where custom-FDID registrations persist; set by the FileSystem facade to
        `<project>/.wowlib/custom-listfile.csv`. */
    void set_sidecar(std::filesystem::path sidecar) { sidecar_ = std::move(sidecar); }

    [[=welder::mark::include,
      =welder::doc("Which storage technology backs this composition."),
      =welder::returns("Mpq or Casc")]]
    static constexpr StorageKind kind() { return Backend::kind(); }

    /** The storage backend. C++-only (backends are not welded). */
    Backend& backend() { return backend_; }

    /** The listfile database. C++-only (providers are not welded). */
    Listfile& listfile() { return listfile_; }

    [[=welder::mark::include,
      =welder::doc("The project-directory overlay, if one is set."),
      =welder::return_policy(welder::rv::reference_internal)]]
    ProjectDirectory* project() { return project_ ? &*project_ : nullptr; }

  private:
    Result<void> save_sidecar()
    {
      if constexpr (requires { listfile_.save_custom_entries(*sidecar_); })
        return listfile_.save_custom_entries(*sidecar_);
      else
        return {};
    }

    Backend backend_;
    Listfile listfile_;
    std::optional<ProjectDirectory> project_;
    std::optional<std::filesystem::path> sidecar_;
  };
}