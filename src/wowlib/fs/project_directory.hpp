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

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::fs
{
  class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A local directory acting as the ultimate patch: files here override every
        client archive, and new files are added here. Lookups go through an
        in-memory index keyed by canonical path, so they are case-insensitive
        regardless of the disk's filesystem and O(1).)")
  ]]
  ProjectDirectory
  {
  public:
    ProjectDirectory() = default;

    [[=welder::doc("Open (and index) a project directory; it is created if missing."),
      =welder::returns("the overlay, or IoError")]]
    static Result<ProjectDirectory> open(
      std::filesystem::path root [[=welder::doc("the directory root")]]);

    [[=welder::doc(R"(
        The real on-disk location of a client-internal path, if the overlay
        carries it.)"),
      =welder::returns("the absolute path, or nothing")]]
    std::optional<std::filesystem::path> resolve(
      std::string_view path [[=welder::doc("the client-internal file path")]]) const;

    [[=welder::doc("Whether the overlay carries a file."),
      =welder::returns("true if present")]]
    bool exists(
      std::string_view path [[=welder::doc("the client-internal file path")]]) const;

    [[=welder::doc("Read an overlay file into memory."),
      =welder::returns("the bytes, or FileNotFound/IoError")]]
    Result<FileBuffer> read(
      std::string_view path [[=welder::doc("the client-internal file path")]]) const;

    [[=welder::doc(R"(
        Write (create or overwrite) a file under the overlay, creating
        intermediate directories, and index it.)"),
      =welder::returns("nothing, or IoError")]]
    Result<void> write(
      std::string_view path [[=welder::doc("the client-internal file path")]],
      std::span<const std::byte> content [[=welder::doc("the file contents")]]);

    [[=welder::doc("Rebuild the index from disk, picking up files changed by other "
                   "tools.")]]
    void rescan();

    [[=welder::doc("The directory root."),
      =welder::returns("the absolute root path")]]
    const std::filesystem::path& root() const { return _root; }

    [[=welder::doc("Number of indexed files."),
      =welder::returns("the file count")]]
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
    mutable std::shared_mutex _mtx;
    std::unordered_map<std::string, std::filesystem::path> _index;   // canonical -> on-disk
  };
}