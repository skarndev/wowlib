#pragma once

/** @file
    The StormLib-backed storage for MPQ-era clients. The header stays
    StormLib-free — archive handles are void* (StormLib's HANDLE). Not welded;
    reached through the FileSystem facade or the ClientFileSystem composition. */

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/mpq/mpq_chain.hpp>

namespace wowlib::fs {
  /** MPQ chain storage. Opens every member of the version's chain — a StormLib
      archive, or a directory of loose files standing in for one — and resolves
      reads through them in reverse load order (last loaded wins), which
      replicates the client's patch override semantics.

      RAII: open() — the only way to obtain an instance — returns a fully open
      storage, so a constructed MpqStorage is an open one. The destructor (and
      move-assignment onto an open storage) closes every archive; the only
      not-open state C++ can hold is a moved-from storage, which is empty and
      safe to destroy.

      Thread safety: reads lock only the archive currently probed (one mutex per
      archive), so reads of different archives proceed in parallel. StormLib
      mutates internal state even on lookups, hence probing locks too. */
  class MpqStorage {
  public:
    /** What to open and how. */
    struct Options {
      std::filesystem::path dataDir; /**< The client's Data/ directory. */
      ClientVersion version; /**< Selects the chain table. */
      Locale locale = Locale::enUS; /**< The locale to open; its Data/{code}/
                                                directory must exist on disk. */
    };

    /** Expand the version's chain and open every archive present on disk.
        @param options what to open.
        @return the open storage, or StorageOpenFailed / ArchiveOpenFailed. */
    static Result<MpqStorage> open(Options options);

    ~MpqStorage() { _close(); }

    MpqStorage(const MpqStorage&) = delete;
    MpqStorage& operator=(const MpqStorage&) = delete;

    MpqStorage(MpqStorage&& other) noexcept
      : _options(std::move(other._options)), _archives(std::move(other._archives)) {}

    MpqStorage& operator=(MpqStorage&& other) noexcept {
      if (this != &other) {
        _close();
        _options = std::move(other._options);
        _archives = std::move(other._archives);
      }
      return *this;
    }

    /** Read a file into memory. MPQ storage is path-addressed: a key without a
        path fails with FdidNotResolvable (resolve ids through a listfile in the
        composition layer first).
        @param key the file identity.
        @return the file bytes, or FileNotFound / FdidNotResolvable. */
    Result<FileBuffer> readFile(const FileKey& key);

    /** Whether the file exists in any archive of the chain.
        @param key the file identity (path required).
        @return true if a read would find it. */
    bool exists(const FileKey& key);

    /** Enumerate every file path reachable through the chain: the members of
        each archive (named by its internal listfile, loaded on demand — the
        archives themselves open with MPQ_OPEN_NO_LISTFILE) plus the loose-dir
        members, canonicalized, deduplicated across the chain and sorted.
        Best-effort by design: an archive that cannot enumerate (no internal
        listfile) is skipped silently, as are StormLib's metadata pseudo-files
        and the nameless hash-table placeholders — a partial listing is more
        useful than none.
        @return the sorted canonical paths, or StorageNotOpen. */
    Result<std::vector<std::string>> enumeratePaths();

    /** @return the storage technology tag (Mpq). */
    static constexpr StorageKind kind() { return StorageKind::Mpq; }

    /** One opened member of the chain, for introspection and tests. Exactly one
        source is active — a StormLib archive, or a loose directory whose files
        are indexed by canonical in-game path — as selected by @ref isDirectory. */
    struct OpenedArchive {
      std::filesystem::path path; /**< The archive file or loose-dir root on disk. */
      bool isDirectory = false; /**< true => served from loose files below. */

      void* handle = nullptr; /**< StormLib HANDLE (archive members). */
      std::unique_ptr<std::mutex> mtx; /**< Serializes StormLib calls (archive members). */

      /** Whether wow-update patch archives are attached (Cata+). A patched
          archive serves files ADDED by its updates too, which StormLib's
          plain has-file probe cannot see — lookups go through the open call
          instead. */
      bool patched = false;

      /** Loose members: canonical in-game path -> the file on disk. Built once at
          open, read-only after, so reads need no lock. Case-insensitive lookup
          falls out of both sides being in canonical (lowercased) form. */
      std::unordered_map<std::string, std::filesystem::path> loose;
    };

    /** @return the opened archives in load order (lowest -> highest priority). */
    std::span<const OpenedArchive> archives() const { return _archives; }

    /** @return the locale the chain was expanded with. */
    Locale locale() const { return _options.locale; }

  private:
    /** Store the options; open() (the factory) performs the work.
        @param options what to open. */
    explicit MpqStorage(Options options)
      : _options(std::move(options)) {}

    /** Expand the chain and open every archive present on disk; called by the
        factory on a fresh instance.
        @return nothing, or StorageOpenFailed / ArchiveOpenFailed. */
    Result<void> _openChain();

    /** Close all archives; safe to call repeatedly. */
    void _close() noexcept;

    /** @return whether archives are held (false only for moved-from storages). */
    bool _isOpen() const { return !_archives.empty(); }

    Options _options;
    std::vector<OpenedArchive> _archives;
  };
}
