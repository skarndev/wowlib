#pragma once

/** @file
    The runtime facade over the static compositions — the primary welder binding
    surface of the fs layer. */

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/client_filesystem.hpp>
#include <wowlib/fs/csv_listfile.hpp>
#include <wowlib/fs/mpq/mpq_storage.hpp>

namespace wowlib::fs
{
  /** The concrete composition for MPQ-era clients (path-addressed, no listfile). */
  using MpqFileSystem = ClientFileSystem<MpqStorage, NullListfile>;

  /** The concrete composition for CASC-era clients (listfile-resolved FileDataIDs). */
  using CascFileSystem = ClientFileSystem<CascStorage, CsvListfile>;

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        Everything needed to open a client filesystem: where the client is, which
        version it is, and the optional listfile / project directory / locale
        configuration.)")
  ]]
  FileSystemSettings
  {
    [[=welder::doc("The client installation root (the directory containing Data/).")]]
    std::filesystem::path client_path;

    [[=welder::doc("The client version; selects the storage backend and MPQ chain.")]]
    ClientVersion version;

    [[=welder::doc(R"(
        Client locale. For MPQ clients nullopt means auto-detect from the Data
        directory.)")]]
    std::optional<Locale> locale;

    [[=welder::doc(R"(
        The project-directory overlay: the ultimate patch, where new files are
        added. Optional.)")]]
    std::optional<std::filesystem::path> project_directory;

    [[=welder::doc(R"(
        The working listfile CSV ('fileDataId;filepath'): read for path<->FileDataID
        resolution and appended to when new files are added. Effectively mandatory
        for modern CASC clients — without it files can only be requested by
        FileDataID.)")]]
    std::optional<std::filesystem::path> listfile_csv;

    [[=welder::doc(R"(
        First FileDataID handed to files added to the project; keep far above
        Blizzard's ~6-7M so future official content never collides.)")]]
    FileDataID custom_fdid_start{1'000'000'000};

    [[=welder::doc("TACT product code for CASC storages ('wow', 'wowt', ...).")]]
    std::string casc_product = "wow";
  };

  class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The runtime gateway to one client's files. Picks the storage backend from
        the client version and hides the static composition behind one bindable
        type; C++ code that wants zero dispatch overhead can use
        MpqFileSystem/CascFileSystem directly.)")
  ]]
  FileSystem
  {
  public:
    [[=welder::doc(R"(
        Initialize a client filesystem: open the storage (the full MPQ chain or
        the CASC storage), load the listfile if given, and attach the
        project-directory overlay.)"),
      =welder::returns("the opened filesystem")]]
    static Result<FileSystem> open(
      FileSystemSettings settings [[=welder::doc("what to open and how")]]);

    [[=welder::doc("Read a file by client-internal path."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> read_file(
      std::string_view path [[=welder::doc("the client-internal file path")]]);

    [[=welder::doc("Read a file by FileDataID."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> read_file(
      FileDataID fdid [[=welder::doc("the numeric file identifier")]]);

    [[=welder::doc("Whether a path is reachable in the overlay or the storage."),
      =welder::returns("true if a read would find it")]]
    bool exists(
      std::string_view path [[=welder::doc("the client-internal file path")]]);

    [[=welder::doc("Whether a FileDataID is reachable."),
      =welder::returns("true if a read would find it")]]
    bool exists(
      FileDataID fdid [[=welder::doc("the numeric file identifier")]]);

    [[=welder::doc(R"(
        Add (or overwrite) a file in the project directory; on CASC-era clients
        new paths get a custom FileDataID, persisted in the working listfile.)"),
      =welder::returns("the file's FileDataID (0 on MPQ-era clients)")]]
    Result<FileDataID> add_file(
      std::string_view path [[=welder::doc("the client-internal path of the file")]],
      std::span<const std::byte> content [[=welder::doc("the file contents")]]);

    [[=welder::doc("Which storage technology backs this filesystem."),
      =welder::returns("Mpq or Casc")]]
    StorageKind kind() const;

    /** The MPQ composition, for C++ callers that want the static types.
        @return the composition, or nullptr if this filesystem is CASC-backed. */
    [[=welder::mark::exclude]]
    MpqFileSystem* mpq() { return std::get_if<MpqFileSystem>(&_impl); }

    /** The CASC composition, for C++ callers that want the static types.
        @return the composition, or nullptr if this filesystem is MPQ-backed. */
    [[=welder::mark::exclude]]
    CascFileSystem* casc() { return std::get_if<CascFileSystem>(&_impl); }

    /** C++-only; target languages construct through open().
        @param impl the ready composition. */
    [[=welder::mark::exclude]]
    explicit FileSystem(std::variant<MpqFileSystem, CascFileSystem> impl)
      : _impl(std::move(impl))
    {
    }

  private:
    std::variant<MpqFileSystem, CascFileSystem> _impl;
  };
}