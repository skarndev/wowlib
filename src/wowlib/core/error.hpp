#pragma once

/** @file
    The error-handling vocabulary: ErrorCode, Error and the Result<T> alias every
    fallible wowlib operation returns. Bindings translate the error branch into
    target-language exceptions. */

#include <cstdint>
#include <expected>
#include <string>

#include <wowlib/core/reflect.hpp>

namespace wowlib
{
  /** Machine-readable failure category carried by every Error. Not welded — the
      Python rod instead generates one exception class per enumerator (the class
      identity IS the code); Lua errors carry the spelling as a message prefix. */
  enum class ErrorCode : std::uint32_t
  {
    StorageOpenFailed,   /**< The client storage (MPQ chain / CASC) failed to initialize. */
    ArchiveOpenFailed,   /**< A single archive within an MPQ chain failed to open. */
    StorageNotOpen,      /**< Operation on a closed or moved-from storage. */
    FileNotFound,        /**< The file exists nowhere in the overlay or storage. */
    PathNotResolvable,   /**< No FileDataID is known for the given path (listfile miss). */
    FdidNotResolvable,   /**< No path is known for the given FileDataID. */
    ListfileParseError,  /**< Malformed listfile CSV content. */
    ListfileIoError,     /**< The listfile could not be read or written. */
    FdidSpaceExhausted,  /**< The custom FileDataID allocator ran out of u32 space. */
    DuplicatePath,       /**< Registering a path that already has a FileDataID. */
    InvalidPath,         /**< A path that cannot be normalized/used. */
    IoError,             /**< Generic filesystem I/O failure (project directory). */
    EncryptedContent,    /**< Content is behind an unknown TACT encryption key. */
    NotSupported,        /**< Operation not supported by this backend/provider. */
    NotImplemented,      /**< Placeholder during phased implementation. */
    BackendError,        /**< Unclassified StormLib/CascLib failure; see native_error. */

    ChunkTruncated,      /**< A chunk header or payload overruns the file buffer. */
    ChunkSizeMismatch,   /**< A chunk's size disagrees with its wire struct layout. */
    ChunkMissing,        /**< A chunk the format requires is absent from the file. */
    OffsetOutOfBounds,   /**< An offset array (M2Array) points outside its base buffer. */
    InvalidEntityState,  /**< An entity's members disagree (e.g. a stored count vs baked satellites). */
    FormatVersionMismatch,    /**< The file's version chunk disagrees with the requested version. */
    UnsupportedClientVersion, /**< No format instantiation exists for the requested client version. */

    TableTruncated,      /**< A client-database header, record block or satellite block overruns the file. */
    TableMagicUnknown,   /**< A client-database magic wowlib does not support for the requested version. */
    SchemaMismatch       /**< A client-database record layout disagrees with the generated WoWDBDefs schema. */
  };

  /** The enumerator spelling of @a code, obtained via reflection.
      @param code the error code.
      @return a static string, never dangling. */
  constexpr std::string_view to_string(ErrorCode code) { return enum_name(code); }

  /** A wowlib operation failure: a machine-readable code, a human-readable
      message, and the originating native (StormLib/CascLib/OS) error value when
      one exists. Not welded — bindings translate it into target-language
      exceptions instead. */
  struct Error
  {
    ErrorCode code;              /**< The failure category. */
    std::string message;         /**< Human-readable description of what failed, with context. */

    /** Raw GetCascError()/GetLastError() value from the native library, 0 if not
        applicable. */
    std::uint32_t native_error = 0;
  };

  /** Every fallible wowlib operation returns `Result<T>`; bindings translate the
      error branch into a target-language exception.
      @tparam T the success payload (`void` for pure effects). */
  template <typename T>
  using Result = std::expected<T, Error>;

  /** Shorthand for constructing the error branch of a Result.
      @param code         the failure category.
      @param message      human-readable context.
      @param native_error raw native library error value, if any.
      @return an unexpected convertible to any Result<T>. */
  inline std::unexpected<Error> make_error(ErrorCode code, std::string message,
                                           std::uint32_t native_error = 0)
  {
    return std::unexpected(Error{code, std::move(message), native_error});
  }
}