#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include <welder/vocabulary.hpp>

namespace wowlib
{
  /** Machine-readable failure category carried by every wowlib::Error. Enum is not
      welded directly; bindings surface errors as exceptions typed by this code. */
  enum class ErrorCode : std::uint32_t
  {
    StorageOpenFailed,   /**< The client storage (MPQ chain / CASC) failed to initialize. */
    ArchiveOpenFailed,   /**< A single archive within an MPQ chain failed to open. */
    StorageNotOpen,      /**< Operation on a closed or moved-from storage. */
    FileNotFound,        /**< The file exists nowhere in the overlay or storage. */
    PathNotResolvable,   /**< No FileDataID is known for the given path (listfile miss). */
    FdidNotResolvable,   /**< No path is known for the given FileDataID. */
    ListfileParseError,  /**< Malformed listfile CSV content. */
    ListfileIoError,     /**< Listfile or sidecar file could not be read/written. */
    FdidSpaceExhausted,  /**< The custom FileDataID allocator ran out of u32 space. */
    DuplicatePath,       /**< Registering a path that already has a FileDataID. */
    InvalidPath,         /**< A path that cannot be normalized/used. */
    IoError,             /**< Generic filesystem I/O failure (project directory). */
    EncryptedContent,    /**< Content is behind an unknown TACT encryption key. */
    NotSupported,        /**< Operation not supported by this backend/provider. */
    NotImplemented,      /**< Placeholder during phased implementation. */
    BackendError         /**< Unclassified StormLib/CascLib failure; see native_error. */
  };

  /** The stable name of @a code (mirrors the enumerator spelling).
      @param code the error code.
      @return a static string, never dangling. */
  std::string_view to_string(ErrorCode code);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A wowlib operation failure: a machine-readable code, a human-readable "
                 "message, and the originating native (StormLib/CascLib/OS) error value "
                 "when one exists.")
  ]]
  Error
  {
    ErrorCode code;

    [[=welder::doc("Human-readable description of what failed, with context.")]]
    std::string message;

    [[=welder::doc("Raw GetCascError()/GetLastError() value from the native library, "
                   "0 if not applicable.")]]
    std::uint32_t native_error = 0;
  };

  /** The core error-handling vocabulary: every fallible wowlib operation returns
      `Result<T>`; bindings translate the error branch into a target-language
      exception. */
  template <typename T>
  using Result = std::expected<T, Error>;

  /** Shorthand for constructing the error branch of a Result.
      @param code         the failure category.
      @param message      human-readable context.
      @param native_error raw native library error value, if any. */
  inline std::unexpected<Error> make_error(ErrorCode code, std::string message,
                                           std::uint32_t native_error = 0)
  {
    return std::unexpected(Error{code, std::move(message), native_error});
  }
}