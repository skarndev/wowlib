#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>

namespace wowlib
{
  // Internal (not welded directly; reached through the FileSystem facade or the
  // ClientFileSystem composition): the CascLib-backed storage for CASC-era clients.
  // The header stays CascLib-free — the storage handle is void* (CascLib's HANDLE).

  /** Local CASC storage. FileDataID is the primary address; opening by name is a
      best-effort fallback that only works on clients whose root manifest still
      carries name hashes (pre-8.2) — on modern clients resolve paths through a
      listfile in the composition layer instead.

      Thread safety: one mutex around the storage handle for the whole
      open-size-read-close sequence; CascLib handles are not documented
      thread-safe. A storage-handle pool is a possible future upgrade (memory-heavy
      — measure first). */
  class CascStorage
  {
  public:
    struct Options
    {
      std::filesystem::path client_root;   /**< Client install root (parent of Data/). */
      std::string product = "wow";         /**< TACT product code. */
      Locale locale = Locale::enUS;        /**< Locale mask for content selection. */
      std::optional<std::uint32_t> build;  /**< Preferred client build; orders the
                                                build-config candidates when a repack
                                                without .build.info is opened. */
    };

    explicit CascStorage(Options options)
      : options_(std::move(options))
    {
    }

    ~CascStorage() { close(); }

    CascStorage(const CascStorage&) = delete;
    CascStorage& operator=(const CascStorage&) = delete;

    CascStorage(CascStorage&& other) noexcept
      : options_(std::move(other.options_))
      , storage_(other.storage_)
    {
      other.storage_ = nullptr;
    }

    CascStorage& operator=(CascStorage&& other) noexcept
    {
      if (this != &other)
      {
        close();
        options_ = std::move(other.options_);
        storage_ = other.storage_;
        other.storage_ = nullptr;
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

    bool is_open() const { return storage_ != nullptr; }

    /** Read a file into memory. Prefers the FileDataID; falls back to open-by-name
        for path-only keys (pre-8.2 clients only).
        @param key the file identity.
        @return the bytes, or FileNotFound / PathNotResolvable / EncryptedContent. */
    Result<FileBuffer> read_file(const FileKey& key);

    /** Whether the file can be opened (probe open + close). */
    bool exists(const FileKey& key);

    static constexpr StorageKind kind() { return StorageKind::Casc; }

  private:
    Options options_;
    void* storage_ = nullptr;             // CascLib HANDLE
    mutable std::mutex mtx_;
  };
}