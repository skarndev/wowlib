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
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/client_filesystem.hpp>
#include <wowlib/fs/client_install.hpp>
#include <wowlib/fs/csv_listfile.hpp>
#include <wowlib/fs/mpq/mpq_storage.hpp>

namespace wowlib::fs {
  /** The concrete composition for MPQ-era clients (path-addressed, no listfile). */
  using MpqFileSystem = ClientFileSystem<MpqStorage, NullListfile>;

  /** The concrete composition for CASC-era clients (listfile-resolved FileDataIDs). */
  using CascFileSystem = ClientFileSystem<CascStorage, CsvListfile>;

  /** The first FileDataID handed to project files by default — far above
      Blizzard's ~6-7M so future official content never collides. */
  inline constexpr FileDataID default_custom_fdid_start{1'000'000'000};

  struct [[
      =welder::weld,
      =welder::doc(R"(
        Everything needed to open a client filesystem: where the client is, which
        version it is, and the optional listfile / project directory / locale
        configuration. An immutable value, fully described at construction — a
        settings object can never be half-edited into an inconsistent state. In
        C++ it stays an aggregate (designated initializers; const members); the
        scripting languages construct through the synthesized field constructor,
        where everything after the version is optional.)")
    ]] FileSystemSettings {
    [[=welder::doc(
      "The client installation root (the directory containing Data/).")]]
    const std::filesystem::path client_path;

    [[=welder::doc(
      "The client version; selects the storage backend and MPQ chain.")]]
    const ClientVersion version;

    [[=welder::doc(R"(
        Client locale. For MPQ clients its Data/{code}/ directory must exist; for
        CASC clients it is the locale mask for content selection.)")]]
    const Locale locale = Locale::enUS;

    [[=welder::doc(R"(
        The project-directory overlay: the ultimate patch, where new files are
        added. Optional.)")]]
    const std::optional<std::filesystem::path> project_directory{};

    [[=welder::doc(R"(
        The working listfile CSV ('fileDataId;filepath'): read for path<->FileDataID
        resolution and appended to when new files are added. Effectively mandatory
        for modern CASC clients — without it files can only be requested by
        FileDataID.)")]]
    const std::optional<std::filesystem::path> listfile_csv{};

    [[=welder::doc(R"(
        First FileDataID handed to files added to the project; keep far above
        Blizzard's ~6-7M so future official content never collides.)")]]
    const FileDataID custom_fdid_start{default_custom_fdid_start};

    [[=welder::doc(R"(
        TACT product code for CASC storages ('wow', 'wow_classic_era', 'wowt',
        ...). Left unset it is derived from the version's flavor
        (ClientVersion.default_casc_product), which is right for every ordinary
        install; set it explicitly for PTR, beta and one-off products
        ('wow_classic_titan'), or let ClientInstall.detect read the exact code
        off the installation.)")]]
    const std::optional<std::string> casc_product{};

    [[=welder::doc(R"(
        Settings for a client whose version is read off the installation itself
        (see ClientInstall.detect) instead of being spelled out — the reliable
        way to open anything Classic, where the build number decides which
        engine's file formats you get. The remaining arguments are the same
        optional configuration the constructor takes.

        Example:
            ```python
            settings = FileSystemSettings.detect("/Games/World of Warcraft/_classic_era_",
                                                 listfile_csv="listfile.csv")
            with FileSystem.open(settings) as fs:
                ...     # formats resolve to the engine the install really is
            ```)"),
      =welder::returns("the settings, or the error ClientInstall.detect raised")
    ]]
    static Result<FileSystemSettings> detect(
      std::filesystem::path client_path [[=welder::doc("the installation directory holding Data/")]],
      Locale locale [[=welder::doc("client locale")]] = Locale::enUS,
      std::optional<std::filesystem::path> project_directory [[=welder::doc("the project-directory overlay")]] = {},
      std::optional<std::filesystem::path> listfile_csv [[=welder::doc("the working listfile CSV")]] = {},
      FileDataID custom_fdid_start [[=welder::doc("first FileDataID for added files")]] = default_custom_fdid_start);
  };

  class [[
      =welder::weld,
      =welder::policy::weld_protected,
      =welder::doc(R"(
        The runtime gateway to one client's files. Picks the storage backend from
        the client version and hides the static composition behind one bindable
        type; C++ code that wants zero dispatch overhead can use
        MpqFileSystem/CascFileSystem directly.

        Lifetime: open() returns an open filesystem and destruction releases it —
        in C++ that is the whole story (RAII; no public close, so no reachable
        wrong state beyond a moved-from object). The scripting languages, where
        finalization timing belongs to the runtime, additionally get explicit
        control: close() and is_open (welded from the protected surface), and in
        Python the with-statement.

        Examples:
            Preferred: scope the storage with a `with` block — `open()` returns
            the filesystem, `__enter__` hands it to the block, and `__exit__`
            calls `close()` on the way out, exception or not:

            ```python
            from wowlib import versions
            from wowlib.fs import FileSystem, FileSystemSettings

            settings = FileSystemSettings("/Games/WoW 3.3.5a", versions.wotlk)
            with FileSystem.open(settings) as fs:
                data = fs.read_file("Interface/FrameXML/UIParent.lua")
            ```

            Equivalent explicit form, when the lifetime cannot be a single block
            (a viewer holding the storage across UI events):

            ```python
            fs = FileSystem.open(settings)
            try:
                data = fs.read_file("Interface/FrameXML/UIParent.lua")
            finally:
                fs.close()      # storage released now, not at GC time
            ```)")]]
    FileSystem {
  public:
    [[=welder::doc(R"(
        Initialize a client filesystem: open the storage (the full MPQ chain or
        the CASC storage), load the listfile if given, and attach the
        project-directory overlay.)"),
      =welder::returns("the opened filesystem")]]
    static Result<FileSystem> open(FileSystemSettings settings [[=welder::doc("what to open and how")]]);

    [[=welder::doc(R"(
        Read a file by FileKey — the generic identity for version-independent
        code: whichever half the key carries (path, FileDataID or both), the
        backend uses what it can address and the listfile fills the gap.)"),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> read_file(
      const FileKey& key [[=welder::doc("the file identity (path, id, or both)")
      ]]);

    [[=welder::doc("Read a file by client-internal path."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> read_file(std::string_view path [[=welder::doc("the client-internal file path")]]) {
      return read_file(FileKey{path});
    }

    [[=welder::doc("Read a file by FileDataID."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> read_file(FileDataID fdid [[=welder::doc("the numeric file identifier")]]) {
      return read_file(FileKey{fdid});
    }

    [[=welder::doc("Whether a file is reachable in the overlay or the storage.")
      ,
      =welder::returns("true if a read would find it")]]
    bool exists(
      const FileKey& key [[=welder::doc("the file identity (path, id, or both)")
      ]]);

    [[=welder::doc("Whether a path is reachable."),
      =welder::returns("true if a read would find it")]]
    bool exists(std::string_view path [[=welder::doc("the client-internal file path")]]) {
      return exists(FileKey{path});
    }

    [[=welder::doc("Whether a FileDataID is reachable."),
      =welder::returns("true if a read would find it")]]
    bool exists(FileDataID fdid [[=welder::doc("the numeric file identifier")]]) {
      return exists(FileKey{fdid});
    }

    [[=welder::doc(R"(
        Enumerate every client-internal file path reachable in the storage:
        the union of the MPQ chain's archive listings (loose directories
        included), or — on CASC clients — every FileDataID the storage holds
        that the loaded listfile can name (unnamed ids are skipped, and
        without a listfile the listing is empty). Paths come back canonical,
        deduplicated and sorted; the project-directory overlay is not
        included.)"),
      =welder::returns("the sorted canonical paths")]]
    Result<std::vector<std::string>> enumerate_paths();

    [[=welder::doc(R"(
        Fill the missing half of a key (path or FileDataID) from the listfile,
        best-effort: present halves are preserved, unknown halves stay empty
        (on MPQ-era clients there is no FileDataID space at all). Lets generic
        tools learn a file's full identity without storage-specific code.)"),
      =welder::returns("the completed key")]]
    FileKey resolve(const FileKey& key [[=welder::doc("the file identity to complete")]]) const;

    [[=welder::doc(R"(
        Add (or overwrite) a file in the project directory; on CASC-era clients
        new paths get a custom FileDataID, persisted in the working listfile.)")
      ,
      =welder::returns("the file's FileDataID (0 on MPQ-era clients)")]]
    Result<FileDataID> add_file(
      std::string_view path [[=welder::doc(
        "the client-internal path of the file")]],
      std::span<const std::byte> content [[=welder::doc("the file contents")]]);

    /** Register TACT encryption keys (CASC clients only) so the storage can
        decrypt content behind them — including the encrypted sections of a .db2,
        which then decode normally instead of being reported encrypted. Takes the
        community "KeyName KeyHex" per-line text format.
        @param key_list the newline-separated key list.
        @return nothing, or NotSupported on an MPQ client, or a backend error. */
    [[=welder::doc(
        "Register TACT encryption keys (CASC only) from the community "
        "'KeyName KeyHex' per-line text, so encrypted .db2 sections "
        "decrypt and decode."),
      =welder::returns("nothing; raises on an MPQ client or a malformed list")]]
    Result<void> import_keys(std::string_view key_list [[=welder::doc("newline-separated 'KeyName KeyHex' lines")]]) {
      if (auto* casc = std::get_if<CascFileSystem>(&_impl)) return casc->backend().import_keys(key_list);
      return make_error(ErrorCode::NotSupported, "TACT encryption keys apply only to CASC (WoD+) clients");
    }

    /** Register one TACT encryption key (CASC clients only). C++-only; scripting
        callers use import_keys with the text format.
        @param key_name the 64-bit key lookup (a section's tact_key_hash).
        @param key      the 16-byte key.
        @return nothing, or NotSupported on an MPQ client, or a backend error. */
    [[=welder::mark::exclude]]
    Result<void> add_encryption_key(std::uint64_t key_name, std::span<const std::byte, 16> key) {
      if (auto* casc = std::get_if<CascFileSystem>(&_impl)) return casc->backend().add_encryption_key(key_name, key);
      return make_error(ErrorCode::NotSupported, "TACT encryption keys apply only to CASC (WoD+) clients");
    }

    [[=welder::getter,
      =welder::doc(
        "Which storage technology backs this filesystem: Mpq or Casc. "
        "A static fact of the opened client; remains valid after close().")]]
    StorageKind kind() const { return _kind; }

    [[=welder::getter,
      =welder::doc("The client version this filesystem was opened for — the "
        "anchor of version-agnostic code (expansion_of, for_version, "
        "Table.open). A static fact of the opened client; remains "
        "valid after close().")]]
    ClientVersion version() const { return _version; }

    /** The MPQ composition, for C++ callers that want the static types.
        @return the composition, or nullptr if this filesystem is CASC-backed. */
    [[=welder::mark::exclude]]
    MpqFileSystem* mpq() { return std::get_if<MpqFileSystem>(&_impl); }

    /** The CASC composition, for C++ callers that want the static types.
        @return the composition, or nullptr if this filesystem is MPQ-backed. */
    [[=welder::mark::exclude]]
    CascFileSystem* casc() { return std::get_if<CascFileSystem>(&_impl); }

    /** C++-only; target languages construct through open().
        @param impl    the ready MPQ composition.
        @param version the client version the composition was opened for. */
    [[=welder::mark::exclude]]
    explicit FileSystem(MpqFileSystem impl, ClientVersion version)
      : _impl(std::move(impl)), _kind(StorageKind::Mpq), _version(version) {}

    /** C++-only; target languages construct through open().
        @param impl    the ready CASC composition.
        @param version the client version the composition was opened for. */
    [[=welder::mark::exclude]]
    explicit FileSystem(CascFileSystem impl, ClientVersion version)
      : _impl(std::move(impl)), _kind(StorageKind::Casc), _version(version) {}

  protected:
    // Welded through policy::weld_protected, uncallable from C++ (where RAII is
    // the lifetime story); scripting languages get deterministic release.

    [[=welder::doc(R"(
        Release the client storage now — every MPQ archive handle or the CASC
        storage handle — instead of waiting for garbage collection. Safe to call
        repeatedly; afterwards reads and writes raise StorageNotOpen. In Python,
        prefer the with-statement, which calls this on block exit.)")]]
    void close() { _impl = std::monostate{}; }

    [[=welder::getter,
      =welder::doc(
        "Whether the filesystem still holds its storage (false after "
        "close()).")]]
    bool is_open() const {
      return !std::holds_alternative<std::monostate>(_impl);
    }

  private:
    // monostate = closed; reachable only through close() above, so C++ callers
    // never observe it (a moved-from FileSystem still holds a composition).
    std::variant<std::monostate, MpqFileSystem, CascFileSystem> _impl;
    StorageKind _kind;
    ClientVersion _version{};
  };
}
