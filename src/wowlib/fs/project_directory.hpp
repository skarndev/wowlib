#pragma once

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib
{
  class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::policy::opt_in,
    =welder::doc("A local directory acting as the ultimate patch: files here override "
                 "every client archive, and new files are added here. Lookups go "
                 "through an in-memory index keyed by canonical path, so they are "
                 "case-insensitive regardless of the disk's filesystem and O(1).")
  ]]
  ProjectDirectory
  {
  public:
    ProjectDirectory() = default;

    [[=welder::mark::include,
      =welder::doc("Open (and index) a project directory; it is created if missing."),
      =welder::returns("the overlay, or IoError")]]
    static Result<ProjectDirectory> open(
      [[=welder::doc("the directory root")]] std::filesystem::path root);

    [[=welder::mark::include,
      =welder::doc("The real on-disk location of a client-internal path, if the "
                   "overlay carries it."),
      =welder::returns("the absolute path, or nothing")]]
    std::optional<std::filesystem::path> resolve(
      [[=welder::doc("the client-internal file path")]] std::string_view path) const;

    [[=welder::mark::include,
      =welder::doc("Whether the overlay carries a file."),
      =welder::returns("true if present")]]
    bool exists(
      [[=welder::doc("the client-internal file path")]] std::string_view path) const;

    [[=welder::mark::include,
      =welder::doc("Read an overlay file into memory."),
      =welder::returns("the bytes, or FileNotFound/IoError")]]
    Result<FileBuffer> read(
      [[=welder::doc("the client-internal file path")]] std::string_view path) const;

    [[=welder::mark::include,
      =welder::doc("Write (create or overwrite) a file under the overlay, creating "
                   "intermediate directories, and index it."),
      =welder::returns("nothing, or IoError")]]
    Result<void> write(
      [[=welder::doc("the client-internal file path")]] std::string_view path,
      [[=welder::doc("the file contents")]] std::span<const std::byte> content);

    [[=welder::mark::include,
      =welder::doc("Rebuild the index from disk, picking up files changed by other "
                   "tools.")]]
    void rescan();

    [[=welder::mark::include,
      =welder::doc("The directory root."),
      =welder::returns("the absolute root path")]]
    const std::filesystem::path& root() const { return root_; }

    /** Number of indexed files (tests/diagnostics). */
    std::size_t size() const;

  private:
    void index_tree_locked();

    std::filesystem::path root_;
    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string, std::filesystem::path> index_;   // canonical -> on-disk

  public:
    ProjectDirectory(const ProjectDirectory&) = delete;
    ProjectDirectory& operator=(const ProjectDirectory&) = delete;
    ProjectDirectory(ProjectDirectory&& other) noexcept;
    ProjectDirectory& operator=(ProjectDirectory&& other) noexcept;
  };
}