#pragma once

/** @file
    The owning byte buffer file contents are read into. */

#include <cstddef>
#include <vector>

namespace wowlib
{
  /** Owning byte buffer for file contents read out of a client storage.

      A plain vector: cheap to move, maps onto the Python buffer protocol, and has
      no lifetime coupling to storage handles (both StormLib and CascLib require
      copying out of the archive anyway). A zero-copy arena can replace this alias
      later without touching call sites. */
  using FileBuffer = std::vector<std::byte>;
}