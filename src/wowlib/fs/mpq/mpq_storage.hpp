#pragma once

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

namespace wowlib
{
  // Internal (not welded directly; reached through the FileSystem facade or the
  // ClientFileSystem composition): the StormLib-backed storage for MPQ-era clients.
  // The header stays StormLib-free — archive handles are void* (StormLib's HANDLE).

  /** MPQ chain storage. Opens every archive of the version's chain standalone and
      resolves reads through them in reverse load order (last loaded wins), which
      replicates the client's patch override semantics.

      Thread safety: reads lock only the archive currently probed (one mutex per
      archive), so reads of different archives proceed in parallel. StormLib
      mutates internal state even on lookups, hence probing locks too. */
  class MpqStorage
  {
  public:
    struct Options
    {
      std::filesystem::path data_dir;      /**< The client's Data/ directory. */
      ClientVersion version;               /**< Selects the chain table. */
      std::optional<Locale> locale;        /**< nullopt => auto-detect from disk. */
    };

    explicit MpqStorage(Options options)
      : options_(std::move(options))
    {
    }

    ~MpqStorage() { close(); }

    MpqStorage(const MpqStorage&) = delete;
    MpqStorage& operator=(const MpqStorage&) = delete;
    MpqStorage(MpqStorage&&) noexcept = default;
    MpqStorage& operator=(MpqStorage&&) noexcept = default;

    /** Expand the chain and open every archive present on disk.
        @return nothing, or StorageOpenFailed / ArchiveOpenFailed. */
    Result<void> open();

    /** Close all archives; safe to call repeatedly. */
    void close() noexcept;

    bool is_open() const { return !archives_.empty(); }

    /** Read a file into memory. MPQ storage is path-addressed: a key without a
        path fails with FdidNotResolvable (resolve ids through a listfile in the
        composition layer first).
        @param key the file identity.
        @return the file bytes, or FileNotFound / FdidNotResolvable. */
    Result<FileBuffer> read_file(const FileKey& key);

    /** Whether the file exists in any archive of the chain. */
    bool exists(const FileKey& key);

    static constexpr StorageKind kind() { return StorageKind::Mpq; }

    /** One opened archive of the chain, for introspection and tests. */
    struct OpenedArchive
    {
      std::filesystem::path path;
      void* handle = nullptr;                    // StormLib HANDLE
      std::unique_ptr<std::mutex> mtx;
    };

    /** The opened archives in load order (lowest -> highest priority). */
    std::span<const OpenedArchive> archives() const { return archives_; }

    /** The locale the chain was expanded with (set by open()). */
    std::optional<Locale> locale() const { return locale_; }

  private:
    Options options_;
    std::optional<Locale> locale_;
    std::vector<OpenedArchive> archives_;
  };
}