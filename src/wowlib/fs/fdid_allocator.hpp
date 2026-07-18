#pragma once

/** @file
    The monotonic allocator behind custom (non-Blizzard) FileDataIDs. An
    implementation detail of listfile providers. */

#include <cstdint>
#include <format>

#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>

namespace wowlib::fs::detail
{
  /** Monotonic allocator for custom (non-Blizzard) FileDataIDs.

      Starts at a configurable point far above Blizzard's allocation (~6-7M as of
      2026) so future official content never collides with project files. Pure and
      single-threaded by itself; the owning listfile provider serializes access
      under its exclusive lock. */
  class FdidAllocator
  {
  public:
    /** @param start the first id to hand out.
        @param max   the last usable id (inclusive); defaults to just below the
                     u32 ceiling. */
    explicit FdidAllocator(FileDataID start = FileDataID{1'000'000'000},
                           FileDataID max = FileDataID{0xFFFFFFFE})
      : _next(start.value)
      , _max(max.value)
    {
    }

    /** Allocate the next id.
        @return the id, or FdidSpaceExhausted once max is passed. */
    Result<FileDataID> next()
    {
      if (_next > _max)
        return make_error(ErrorCode::FdidSpaceExhausted,
                          std::format("custom FileDataID space exhausted at {}", _max));
      return FileDataID{static_cast<std::uint32_t>(_next++)};
    }

    /** Bump the allocator past an id seen in a loaded listfile, so re-opening a
        project never re-hands-out an id already in use.
        @param id an existing id (only ids inside the custom range move the cursor). */
    void note_existing(FileDataID id)
    {
      if (id.value >= _next && id.value <= _max)
        _next = id.value + 1;
    }

    /** The id the next successful call to next() would return.
        @return the upcoming id. */
    FileDataID peek() const { return FileDataID{static_cast<std::uint32_t>(_next)}; }

  private:
    std::uint64_t _next;   // u64 internally so _next can pass _max without wrapping
    std::uint64_t _max;
  };
}