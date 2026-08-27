#pragma once

/** @file
    The static composition of one client's file access. C++-only (not welded):
    target languages construct through fs::FileSystem, C++ callers wanting zero
    dispatch overhead use the concrete compositions directly. */

#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/listfile.hpp>
#include <wowlib/fs/project_directory.hpp>
#include <wowlib/fs/storage_backend.hpp>

namespace wowlib::fs {
  /** The static composition of one client's file access: storage backend +
      listfile database + optional project-directory overlay. Lookup precedence:
      project directory first (the ultimate patch), then the client storage.

      @tparam Backend  the storage backend (MpqStorage or CascStorage).
      @tparam Listfile the path<->FileDataID database provider. */
  template <StorageBackend Backend, ListfileProvider Listfile = NullListfile>
  class ClientFileSystem {
  public:
    /** Compose from ready components (backends are open by construction).
        @param backend  the open storage backend.
        @param listfile the listfile database.
        @param project  the overlay, if any. */
    ClientFileSystem(Backend backend, Listfile listfile = {}, std::optional<ProjectDirectory> project = std::nullopt)
      : _backend(std::move(backend)), _listfile(std::move(listfile)), _project(std::move(project)) {}

    /** Read a file by key. Missing identity halves are filled from the listfile;
        the project directory wins over the client storage.
        @param key the file identity (path, id, or both).
        @return the file bytes. */
    Result<FileBuffer> readFile(const FileKey& key) {
      const FileKey resolved = resolve(key);

      if (_project && resolved.path && _project->exists(*resolved.path)) return _project->read(*resolved.path);

      return _backend.readFile(resolved);
    }

    /** Whether a file is reachable in the overlay or the storage.
        @param key the file identity.
        @return true if a read would find it. */
    bool exists(const FileKey& key) {
      const FileKey resolved = resolve(key);
      if (_project && resolved.path && _project->exists(*resolved.path)) return true;
      return _backend.exists(resolved);
    }

    /** Fill the missing half of a key (path or FileDataID) from the listfile,
        best-effort.
        @param key the file identity.
        @return the completed key (unchanged parts preserved). */
    FileKey resolve(const FileKey& key) const {
      FileKey out = key;
      if (!out.fdid && out.path) out.fdid = _listfile.pathToFdid(*out.path);
      else if (!out.path && out.fdid) out.path = _listfile.fdidToPath(*out.fdid);
      return out;
    }

    /** Add (or overwrite) a file in the project directory. On a CASC-era client a
        new path is also registered in the listfile, allocating a custom
        FileDataID (the provider persists it to its working file).
        @param path    the client-internal path of the file.
        @param content the file contents.
        @return the file's FileDataID (0 on MPQ-era clients, which have no id
                space). */
    Result<FileDataID> addFile(std::string_view path, std::span<const std::byte> content) {
      if (!_project)
        return makeError(ErrorCode::NotSupported, "no project directory is set; there is nowhere to add files");

      if (auto written = _project->write(path, content); !written) return std::unexpected(written.error());

      if constexpr (Backend::kind() == StorageKind::Mpq) return FileDataID{0};
      else {
        // Overwriting an already-known file keeps its id; only new paths allocate.
        if (auto existing = _listfile.pathToFdid(path)) return *existing;
        return _listfile.registerPath(path);
      }
    }

    /** @return the storage technology backing this composition. */
    static constexpr StorageKind kind() { return Backend::kind(); }

    /** @return the storage backend. */
    Backend& backend() { return _backend; }

    /** @return the listfile database. */
    Listfile& listfile() { return _listfile; }

    /** @return the project-directory overlay, or nullptr if none is set. */
    ProjectDirectory* project() { return _project ? &*_project : nullptr; }

  private:
    Backend _backend;
    Listfile _listfile;
    std::optional<ProjectDirectory> _project;
  };
}
