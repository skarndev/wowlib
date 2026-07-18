#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <welder/vocabulary.hpp>

#include <wowlib/core/path.hpp>

namespace wowlib
{
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A strongly-typed FileDataID — the numeric file identifier used by "
                 "CASC-era clients (u32, matching the client's root manifest and DB2 "
                 "references).")
  ]]
  FileDataID
  {
    [[=welder::doc("The raw numeric identifier.")]]
    std::uint32_t value = 0;

    constexpr auto operator<=>(const FileDataID&) const = default;
  };

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A file request: by client-internal path, by FileDataID, or both. "
                 "Which part is authoritative depends on the storage backend handling "
                 "the request; the stored path is always in canonical form.")
  ]]
  FileKey
  {
    [[=welder::doc("The numeric identifier, if known.")]]
    std::optional<FileDataID> fdid;

    [[=welder::doc("The canonical client-internal path, if known.")]]
    std::optional<std::string> path;

    [[=welder::doc("Make a key from a path in any accepted spelling; it is "
                   "canonicalized here."),
      =welder::returns("a path-only key")]]
    static FileKey by_path([[=welder::doc("the client-internal file path")]]
                           std::string_view path)
    {
      return {.fdid = std::nullopt, .path = normalize_path(path)};
    }

    [[=welder::doc("Make a key from a FileDataID alone."),
      =welder::returns("an id-only key")]]
    static FileKey by_fdid([[=welder::doc("the numeric file identifier")]]
                           FileDataID fdid)
    {
      return {.fdid = fdid, .path = std::nullopt};
    }

    [[=welder::doc("Make a key carrying both identities of one file."),
      =welder::returns("a key with both path and id set")]]
    static FileKey by_both([[=welder::doc("the client-internal file path")]]
                           std::string_view path,
                           [[=welder::doc("the numeric file identifier")]]
                           FileDataID fdid)
    {
      return {.fdid = fdid, .path = normalize_path(path)};
    }
  };
}

template <>
struct std::hash<wowlib::FileDataID>
{
  std::size_t operator()(const wowlib::FileDataID& id) const noexcept
  {
    return std::hash<std::uint32_t>{}(id.value);
  }
};