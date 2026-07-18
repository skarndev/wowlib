#pragma once

/** @file
    The CSV-backed listfile provider: one working file ('fileDataId;filepath' per
    line) that is both read from and written to. */

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

namespace wowlib::fs
{
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Tuning knobs for loading a listfile.")
  ]]
  CsvListfileOptions
  {
    [[=welder::doc(R"(
        First FileDataID handed to newly registered files; keep far above
        Blizzard's ~6-7M so future official content never collides.)")]]
    FileDataID custom_fdid_start{1'000'000'000};
  };
  // (namespace-scope rather than nested: gcc late-parses nested-class NSDMIs, so a
  // `= {}` default argument cannot aggregate-initialize a nested struct)

  class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The CSV-backed path<->FileDataID database ('fileDataId;filepath' per line,
        the community listfile format of github.com/wowdev/wow-listfile). The
        supplied file is the working database: lookups read from it, registrations
        append to it, so customizations live right next to the community data and
        survive reloads. Thread-safe: concurrent lookups, exclusive registration.)")
  ]]
  CsvListfile
  {
  public:
    using Options = CsvListfileOptions;

    CsvListfile() = default;

    [[=welder::doc(R"(
        Load a listfile CSV; the file becomes the working database that
        register_path appends to. Later lines override earlier ones, and the
        custom-id allocator resumes after the highest custom id present.)"),
      =welder::returns("the loaded database, or ListfileIoError/ListfileParseError")]]
    static Result<CsvListfile> load(
      const std::filesystem::path& csv [[=welder::doc("path to the CSV on disk")]],
      Options options [[=welder::doc("loading options")]] = {});

    [[=welder::doc("Look up the FileDataID of a path (any spelling; canonicalized here)."),
      =welder::returns("the id, or nothing on a miss")]]
    std::optional<FileDataID> path_to_fdid(
      std::string_view path [[=welder::doc("the client-internal file path")]]) const;

    [[=welder::doc("Look up the canonical path of a FileDataID."),
      =welder::returns("the canonical path, or nothing on a miss")]]
    std::optional<std::string> fdid_to_path(
      FileDataID fdid [[=welder::doc("the numeric file identifier")]]) const;

    [[=welder::doc(R"(
        Allocate a fresh custom FileDataID for a new path, remember the mapping and
        append it to the working file (in-memory only when the database was not
        loaded from a file). Fails with DuplicatePath if the path is already
        known.)"),
      =welder::returns("the newly allocated id")]]
    Result<FileDataID> register_path(
      std::string_view path [[=welder::doc("the client-internal path of the new file")]]);

    [[=welder::doc("Whether a path is known."),
      =welder::returns("true if the path has a FileDataID")]]
    bool contains(
      std::string_view path [[=welder::doc("the client-internal file path")]]) const;

    [[=welder::doc("Rewrite the whole working file canonically (ids ascending)."),
      =welder::returns("nothing, or ListfileIoError")]]
    Result<void> save() const;

    [[=welder::doc("Number of known mappings."),
      =welder::returns("the entry count")]]
    std::size_t size() const;

    [[=welder::doc("The working file registrations append to (empty if in-memory)."),
      =welder::returns("the CSV path")]]
    const std::filesystem::path& source() const { return _source; }

    CsvListfile(const CsvListfile&) = delete;
    CsvListfile& operator=(const CsvListfile&) = delete;
    CsvListfile(CsvListfile&& other) noexcept;
    CsvListfile& operator=(CsvListfile&& other) noexcept;

  private:
    std::filesystem::path _source;
    mutable std::shared_mutex _mtx;
    std::unordered_map<std::string, FileDataID> _path_to_id;
    std::unordered_map<std::uint32_t, std::string> _id_to_path;
    detail::FdidAllocator _allocator;
  };

  static_assert(ListfileProvider<CsvListfile>);
}