#pragma once

/** @file
    The project-directory overlay: a local directory acting as the ultimate patch
    over any client storage. */

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/shared_mutex.hpp>

namespace wowlib::fs {
  /** A local directory acting as the ultimate patch: files here override every
      client archive, and new files are added here. Lookups go through an
      in-memory index keyed by canonical path, so they are case-insensitive
      regardless of the disk's filesystem and O(1). Not welded — target languages
      reach it through the FileSystem facade. */
  class ProjectDirectory {
  public:
    ProjectDirectory() = default;

    /** Open (and index) a project directory; it is created if missing.
        @param root the directory root.
        @return the overlay, or IoError. */
    static Result<ProjectDirectory> open(std::filesystem::path root);

    /** The real on-disk location of a client-internal path, if the overlay
        carries it.
        @param path the client-internal file path.
        @return the absolute path, or nothing. */
    std::optional<std::filesystem::path> resolve(std::string_view path) const;

    /** Whether the overlay carries a file.
        @param path the client-internal file path.
        @return true if present. */
    bool exists(std::string_view path) const;

    /** Read an overlay file into memory.
        @param path the client-internal file path.
        @return the bytes, or FileNotFound/IoError. */
    Result<FileBuffer> read(std::string_view path) const;

    /** Write (create or overwrite) a file under the overlay, creating
        intermediate directories, and index it.
        @param path    the client-internal file path.
        @param content the file contents.
        @return nothing, or IoError. */
    Result<void> write(std::string_view path, std::span<const std::byte> content);

    /** Rebuild the index from disk, picking up files changed by other tools. */
    void rescan();

    /** @return the directory root (absolute). */
    const std::filesystem::path& root() const { return _root; }

    /** @return the number of indexed files. */
    std::size_t size() const;

    ProjectDirectory(const ProjectDirectory&) = delete;
    ProjectDirectory& operator=(const ProjectDirectory&) = delete;
    ProjectDirectory(ProjectDirectory&& other) noexcept;
    ProjectDirectory& operator=(ProjectDirectory&& other) noexcept;

  private:
    /** Rebuild _index from the tree under _root; caller holds the exclusive lock
        (or is the only owner). */
    void index_tree_locked();

    std::filesystem::path _root;
    mutable SharedMutex _mtx;
    std::unordered_map<std::string, std::filesystem::path> _index;
    // canonical -> on-disk
  };
}
