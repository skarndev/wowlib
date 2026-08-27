#pragma once

/** @file
    File identity types: the strong FileDataID and the FileKey a read request
    travels as. */

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <welder/vocabulary.hpp>

#include <wowlib/core/path.hpp>

namespace wowlib {
  struct [[
      =welder::weld,
      =welder::doc(R"(
        A strongly-typed FileDataID — the numeric file identifier used by CASC-era
        clients (u32, matching the client's root manifest and DB2 references).)")
    ]] FileDataID {
    [[=welder::doc("The raw numeric identifier.")]]
    std::uint32_t value = 0;

    constexpr auto operator<=>(const FileDataID&) const = default;
  };

  struct [[
      =welder::weld,
      =welder::doc(R"(
        A file request: by client-internal path, by FileDataID, or both. The
        generic file identity for version-independent tools — code that handles
        any client generation operates on FileKeys without caring which half is
        available (on pre-CASC clients the FileDataID is simply absent); the
        storage backend uses the half it needs and FileSystem.resolve fills gaps
        through the listfile. The stored path is always in canonical form.)")
    ]] FileKey {
    [[=welder::doc("The numeric identifier, if known.")]]
    std::optional<FileDataID> fdid;

    [[=welder::doc("The canonical client-internal path, if known.")]]
    std::optional<std::string> path;

    FileKey() = default;

    [[=welder::doc(R"(
        A path-only key; the path is canonicalized here and may use any accepted
        spelling.)")]]
    FileKey(
      std::string_view filePath [[=welder::doc("the client-internal file path")
      ]])
      : path(normalizePath(filePath)) {}

    [[=welder::doc("An id-only key.")]]
    FileKey(FileDataID fileId [[=welder::doc("the numeric file identifier")]])
      : fdid(fileId) {}

    [[=welder::doc("A key carrying both identities of one file.")]]
    FileKey(
      std::string_view filePath [[=welder::doc("the client-internal file path")
      ]],
      FileDataID fileId [[=welder::doc("the numeric file identifier")]])
      : fdid(fileId), path(normalizePath(filePath)) {}
  };
}

template <>
struct std::hash<wowlib::FileDataID> {
  std::size_t operator()(const wowlib::FileDataID& id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value);
  }
};
