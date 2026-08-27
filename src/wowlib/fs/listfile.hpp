#pragma once

/** @file
    The pluggable listfile-provider contract and the no-database provider for
    clients that need none. */

#include <concepts>
#include <optional>
#include <string>
#include <string_view>

#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>

namespace wowlib::fs {
  /** The pluggable path<->FileDataID database contract (CSV today, SQL later).

      All methods are thread-safe by contract: lookups may run concurrently with
      each other and with registerPath. Paths passed in and stored are always in
      canonical form (see normalizePath). Not bound directly — concrete providers
      are.
      @tparam L the provider type under test. */
  template <typename L> concept ListfileProvider = requires(L l, const L cl, std::string_view path, FileDataID id) {
    { cl.pathToFdid(path) } -> std::same_as<std::optional<FileDataID>>; {
      cl.fdidToPath(id)
    } -> std::same_as<std::optional<std::string>>; { l.registerPath(path) } -> std::same_as<Result<FileDataID>>; {
      cl.contains(path)
    } -> std::same_as<bool>;
  };

  /** The no-database provider for clients that need none (MPQ-era). Every lookup
      misses; registration is NotSupported. Satisfies ListfileProvider. */
  struct NullListfile {
    /** Always misses. @return nullopt. */
    std::optional<FileDataID> pathToFdid(std::string_view) const {
      return std::nullopt;
    }

    /** Always misses. @return nullopt. */
    std::optional<std::string> fdidToPath(FileDataID) const {
      return std::nullopt;
    }

    /** Never supported. @return NotSupported. */
    Result<FileDataID> registerPath(std::string_view) {
      return makeError(ErrorCode::NotSupported,
                        "NullListfile cannot register paths; this client has no " "FileDataID space");
    }

    /** Never contained. @return false. */
    bool contains(std::string_view) const { return false; }
  };

  static_assert(ListfileProvider<NullListfile>);
}
