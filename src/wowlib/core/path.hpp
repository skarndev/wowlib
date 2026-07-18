#pragma once

/** @file
    Client-internal path canonicalization. Not welded — bound types accept plain
    strings and normalize at the boundary. */

#include <string>
#include <string_view>

namespace wowlib
{
  /** Canonicalize a client-internal file path.

      Canonical form is lowercase ASCII with backslash separators — the MPQ hashing
      convention — so every map key and comparison in the library uses one
      representation. Forward slashes are converted, duplicate separators collapsed,
      and leading/trailing separators dropped.

      @param path the path in any accepted spelling ("World/Maps/Azeroth.wdt").
      @return the canonical spelling ("world\\maps\\azeroth.wdt"). */
  std::string normalize_path(std::string_view path);

  /** Convert a canonical path to a forward-slash relative path for use on the
      native filesystem (project-directory overlay on POSIX).
      @param canonical a path in canonical form.
      @return the same path with '/' separators. */
  std::string to_native_relative(std::string_view canonical);
}