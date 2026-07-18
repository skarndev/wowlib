#pragma once

/** @file
    The StormLib-backed storage for MPQ-era clients. The header stays
    StormLib-free — archive handles are void* (StormLib's HANDLE). Not welded;
    reached through the FileSystem facade or the ClientFileSystem composition. */

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/fs/mpq/mpq_chain.hpp>

namespace wowlib::fs
{
  /** MPQ chain storage. Opens every archive of the version's chain standalone and
      resolves reads through them in reverse load order (last loaded wins), which
      replicates the client's patch override semantics.

      RAII: the destructor (and move-assignment onto an open storage) closes every
      archive; moved-from storages are empty and safe to destroy.

      Thread safety: reads lock only the archive currently probed (one mutex per
      archive), so reads of different archives proceed in parallel. StormLib
      mutates internal state even on lookups, hence probing locks too. */
  class MpqStorage
  {
  public:
    /** What to open and how. */
    struct Options
    {
      std::filesystem::path data_dir;      /**< The client's Data/ directory. */
      ClientVersion version;               /**< Selects the chain table. */
      std::optional<Locale> locale;        /**< nullopt => auto-detect from disk. */
    };

    /** Store the options; open() performs the work.
        @param options what to open. */
    explicit MpqStorage(Options options)
      : _options(std::move(options))
    {
    }

    ~MpqStorage() { close(); }

    MpqStorage(const MpqStorage&) = delete;
    MpqStorage& operator=(const MpqStorage&) = delete;

    MpqStorage(MpqStorage&& other) noexcept
      : _options(std::move(other._options))
      , _locale(other._locale)
      , _archives(std::move(other._archives))
    {
    }

    MpqStorage& operator=(MpqStorage&& other) noexcept
    {
      if (this != &other)
      {
        close();
        _options = std::move(other._options);
        _locale = other._locale;
        _archives = std::move(other._archives);
      }
      return *this;
    }

    /** Expand the chain and open every archive present on disk.
        @return nothing, or StorageOpenFailed / ArchiveOpenFailed. */
    Result<void> open();

    /** Close all archives; safe to call repeatedly. */
    void close() noexcept;

    /** @return whether open() succeeded and archives are held. */
    bool is_open() const { return !_archives.empty(); }

    /** Read a file into memory. MPQ storage is path-addressed: a key without a
        path fails with FdidNotResolvable (resolve ids through a listfile in the
        composition layer first).
        @param key the file identity.
        @return the file bytes, or FileNotFound / FdidNotResolvable. */
    Result<FileBuffer> read_file(const FileKey& key);

    /** Whether the file exists in any archive of the chain.
        @param key the file identity (path required).
        @return true if a read would find it. */
    bool exists(const FileKey& key);

    /** @return the storage technology tag (Mpq). */
    static constexpr StorageKind kind() { return StorageKind::Mpq; }

    /** One opened archive of the chain, for introspection and tests. */
    struct OpenedArchive
    {
      std::filesystem::path path;                /**< The archive on disk. */
      void* handle = nullptr;                    /**< StormLib HANDLE. */
      std::unique_ptr<std::mutex> mtx;           /**< Serializes StormLib calls. */
    };

    /** @return the opened archives in load order (lowest -> highest priority). */
    std::span<const OpenedArchive> archives() const { return _archives; }

    /** @return the locale the chain was expanded with (set by open()). */
    std::optional<Locale> locale() const { return _locale; }

  private:
    Options _options;
    std::optional<Locale> _locale;
    std::vector<OpenedArchive> _archives;
  };
}