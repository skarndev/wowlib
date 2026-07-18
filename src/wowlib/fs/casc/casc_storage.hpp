#pragma once

/** @file
    The CascLib-backed storage for CASC-era clients. The header stays
    CascLib-free — the storage handle is void* (CascLib's HANDLE). Not welded;
    reached through the FileSystem facade or the ClientFileSystem composition. */

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>

namespace wowlib::fs
{
  /** Local CASC storage. FileDataID is the primary address; opening by name is a
      best-effort fallback that only works on clients whose root manifest still
      carries name hashes (pre-8.2) — on modern clients resolve paths through a
      listfile in the composition layer instead.

      RAII: the destructor (and move-assignment onto an open storage) closes the
      CascLib handle; moved-from storages are empty and safe to destroy.

      Thread safety: one mutex around the storage handle for the whole
      open-size-read-close sequence; CascLib handles are not documented
      thread-safe. A storage-handle pool is a possible future upgrade (memory-heavy
      — measure first). */
  class CascStorage
  {
  public:
    /** What to open and how. */
    struct Options
    {
      std::filesystem::path client_root;   /**< Client install root (parent of Data/). */
      std::string product = "wow";         /**< TACT product code. */
      Locale locale = Locale::enUS;        /**< Locale mask for content selection. */
      std::optional<std::uint32_t> build;  /**< Preferred client build; orders the
                                                build-config candidates when a repack
                                                without .build.info is opened. */
    };

    /** Store the options; open() performs the work.
        @param options what to open. */
    explicit CascStorage(Options options)
      : _options(std::move(options))
    {
    }

    ~CascStorage() { close(); }

    CascStorage(const CascStorage&) = delete;
    CascStorage& operator=(const CascStorage&) = delete;

    CascStorage(CascStorage&& other) noexcept
      : _options(std::move(other._options))
      , _storage(other._storage)
    {
      other._storage = nullptr;
    }

    CascStorage& operator=(CascStorage&& other) noexcept
    {
      if (this != &other)
      {
        close();
        _options = std::move(other._options);
        _storage = other._storage;
        other._storage = nullptr;
      }
      return *this;
    }

    /** Open the local storage. Fallback ladder for repacks (e.g. WoWCircle) that
        ship without a root .build.info: (a) plain open of the client root, then
        Data/; (b) scan Data/config for build configurations, synthesize a
        .build.info in a temp shim directory (with a Data symlink back to the
        client) and try each candidate build config until one opens — candidates
        ordered by Options::build match, then by build number descending.
        @return nothing, or StorageOpenFailed. */
    Result<void> open();

    /** Close the storage; safe to call repeatedly. */
    void close() noexcept;

    /** @return whether the storage handle is held. */
    bool is_open() const { return _storage != nullptr; }

    /** Read a file into memory. Prefers the FileDataID; falls back to open-by-name
        for path-only keys (pre-8.2 clients only).
        @param key the file identity.
        @return the bytes, or FileNotFound / PathNotResolvable / EncryptedContent. */
    Result<FileBuffer> read_file(const FileKey& key);

    /** Whether the file can be opened (probe open + close).
        @param key the file identity.
        @return true if a read would find it. */
    bool exists(const FileKey& key);

    /** @return the storage technology tag (Casc). */
    static constexpr StorageKind kind() { return StorageKind::Casc; }

  private:
    Options _options;
    void* _storage = nullptr;             // CascLib HANDLE
    mutable std::mutex _mtx;
  };
}