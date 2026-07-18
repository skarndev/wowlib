#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <string_view>

#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>

namespace wowlib
{
  /** The pluggable path<->FileDataID database contract (CSV today, SQL later).

      All methods are thread-safe by contract: lookups may run concurrently with
      each other and with register_path. Paths passed in and stored are always in
      canonical form (see normalize_path). Not bound directly — concrete providers
      are. */
  template <typename L>
  concept ListfileProvider = requires(L l, const L cl, std::string_view path, FileDataID id) {
    { cl.path_to_fdid(path) } -> std::same_as<std::optional<FileDataID>>;
    { cl.fdid_to_path(id) } -> std::same_as<std::optional<std::string>>;
    { l.register_path(path) } -> std::same_as<Result<FileDataID>>;
    { cl.contains(path) } -> std::same_as<bool>;
  };

  /** The no-database provider for clients that need none (MPQ-era). Every lookup
      misses; registration is NotSupported. Satisfies ListfileProvider. */
  struct NullListfile
  {
    std::optional<FileDataID> path_to_fdid(std::string_view) const { return std::nullopt; }
    std::optional<std::string> fdid_to_path(FileDataID) const { return std::nullopt; }

    Result<FileDataID> register_path(std::string_view)
    {
      return make_error(ErrorCode::NotSupported,
                        "NullListfile cannot register paths; this client has no "
                        "FileDataID space");
    }

    bool contains(std::string_view) const { return false; }
  };

  static_assert(ListfileProvider<NullListfile>);
}