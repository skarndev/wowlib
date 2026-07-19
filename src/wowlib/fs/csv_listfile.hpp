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

#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/fdid_allocator.hpp>
#include <wowlib/fs/listfile.hpp>

namespace wowlib::fs
{
  /** Tuning knobs for loading a listfile. */
  struct CsvListfileOptions
  {
    /** First FileDataID handed to newly registered files; keep far above
        Blizzard's ~6-7M so future official content never collides. */
    FileDataID custom_fdid_start{1'000'000'000};
  };
  // (namespace-scope rather than nested: gcc late-parses nested-class NSDMIs, so a
  // `= {}` default argument cannot aggregate-initialize a nested struct)

  /** The CSV-backed path<->FileDataID database ('fileDataId;filepath' per line,
      the community listfile format of github.com/wowdev/wow-listfile). The
      supplied file is the working database: lookups read from it, registrations
      append to it, so customizations live right next to the community data and
      survive reloads. Thread-safe: concurrent lookups, exclusive registration.
      Not welded — target languages reach it through the FileSystem facade. */
  class CsvListfile
  {
  public:
    using Options = CsvListfileOptions;

    CsvListfile() = default;

    /** Load a listfile CSV; the file becomes the working database that
        register_path appends to. Later lines override earlier ones, and the
        custom-id allocator resumes after the highest custom id present.
        @param csv     path to the CSV on disk.
        @param options loading options.
        @return the loaded database, or ListfileIoError/ListfileParseError. */
    static Result<CsvListfile> load(const std::filesystem::path& csv,
                                    Options options = {});

    /** Look up the FileDataID of a path (any spelling; canonicalized here).
        @param path the client-internal file path.
        @return the id, or nothing on a miss. */
    std::optional<FileDataID> path_to_fdid(std::string_view path) const;

    /** Look up the canonical path of a FileDataID.
        @param fdid the numeric file identifier.
        @return the canonical path, or nothing on a miss. */
    std::optional<std::string> fdid_to_path(FileDataID fdid) const;

    /** Allocate a fresh custom FileDataID for a new path, remember the mapping and
        append it to the working file (in-memory only when the database was not
        loaded from a file). Fails with DuplicatePath if the path is already known.
        @param path the client-internal path of the new file.
        @return the newly allocated id. */
    Result<FileDataID> register_path(std::string_view path);

    /** Whether a path is known.
        @param path the client-internal file path.
        @return true if the path has a FileDataID. */
    bool contains(std::string_view path) const;

    /** Rewrite the whole working file canonically (ids ascending).
        @return nothing, or ListfileIoError. */
    Result<void> save() const;

    /** @return the number of known mappings. */
    std::size_t size() const;

    /** @return the working file registrations append to (empty if in-memory). */
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