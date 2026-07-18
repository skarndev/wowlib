#pragma once

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <welder/vocabulary.hpp>

#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/fdid_allocator.hpp>
#include <wowlib/fs/listfile.hpp>

namespace wowlib
{
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Tuning knobs for loading a community listfile.")
  ]]
  CsvListfileOptions
  {
    [[=welder::doc("First FileDataID handed to custom files; keep far above "
                   "Blizzard's ~6-7M so future official content never collides.")]]
    FileDataID custom_fdid_start{1'000'000'000};
  };
  // (namespace-scope rather than nested: gcc late-parses nested-class NSDMIs, so a
  // `= {}` default argument cannot aggregate-initialize a nested struct)

  class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::policy::opt_in,
    =welder::doc("The community-listfile-backed path<->FileDataID database "
                 "(github.com/wowdev/wow-listfile, 'fileDataId;filepath' CSV). "
                 "Thread-safe: concurrent lookups, exclusive registration. Custom "
                 "registrations are kept apart from the community data and persist "
                 "to a sidecar CSV so the community file stays pristine.")
  ]]
  CsvListfile
  {
  public:
    using Options = CsvListfileOptions;

    CsvListfile() = default;

    [[=welder::mark::include,
      =welder::doc("Load a community listfile CSV ('fileDataId;filepath' per line)."),
      =welder::returns("the loaded database, or ListfileIoError/ListfileParseError")]]
    static Result<CsvListfile> load(
      [[=welder::doc("path to the CSV on disk")]] const std::filesystem::path& csv,
      [[=welder::doc("loading options")]] Options options = {});

    [[=welder::mark::include,
      =welder::doc("Merge a previously saved sidecar of custom registrations and bump "
                   "the allocator past every id found in it."),
      =welder::returns("nothing, or ListfileIoError/ListfileParseError")]]
    Result<void> load_custom_entries(
      [[=welder::doc("path to the sidecar CSV")]] const std::filesystem::path& sidecar);

    [[=welder::mark::include,
      =welder::doc("Persist only the custom registrations (never community data) as a "
                   "'fileDataId;filepath' CSV."),
      =welder::returns("nothing, or ListfileIoError")]]
    Result<void> save_custom_entries(
      [[=welder::doc("path of the sidecar CSV to write")]] const std::filesystem::path& sidecar) const;

    [[=welder::mark::include,
      =welder::doc("Look up the FileDataID of a path (any spelling; canonicalized here)."),
      =welder::returns("the id, or nothing on a miss")]]
    std::optional<FileDataID> path_to_fdid(
      [[=welder::doc("the client-internal file path")]] std::string_view path) const;

    [[=welder::mark::include,
      =welder::doc("Look up the canonical path of a FileDataID."),
      =welder::returns("the canonical path, or nothing on a miss")]]
    std::optional<std::string> fdid_to_path(
      [[=welder::doc("the numeric file identifier")]] FileDataID fdid) const;

    [[=welder::mark::include,
      =welder::doc("Allocate a fresh custom FileDataID for a new path and remember the "
                   "mapping. Fails with DuplicatePath if the path is already known."),
      =welder::returns("the newly allocated id")]]
    Result<FileDataID> register_path(
      [[=welder::doc("the client-internal path of the new file")]] std::string_view path);

    [[=welder::mark::include,
      =welder::doc("Whether a path is known (community or custom)."),
      =welder::returns("true if the path has a FileDataID")]]
    bool contains(
      [[=welder::doc("the client-internal file path")]] std::string_view path) const;

    [[=welder::mark::include,
      =welder::doc("Number of known mappings, community plus custom."),
      =welder::returns("the entry count")]]
    std::size_t size() const;

  private:
    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string, FileDataID> path_to_id_;
    std::unordered_map<std::uint32_t, std::string> id_to_path_;
    std::unordered_map<std::uint32_t, std::string> custom_entries_;   // subset of id_to_path_
    FdidAllocator allocator_;

  public:
    /** Copy/move: a listfile is heavy and lock-bearing; moves transfer the maps,
        copies are deleted to keep accidental duplication out of hot paths. */
    CsvListfile(const CsvListfile&) = delete;
    CsvListfile& operator=(const CsvListfile&) = delete;
    CsvListfile(CsvListfile&& other) noexcept;
    CsvListfile& operator=(CsvListfile&& other) noexcept;
  };

  static_assert(ListfileProvider<CsvListfile>);
}