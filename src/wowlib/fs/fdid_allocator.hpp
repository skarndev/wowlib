#pragma once

#include <cstdint>
#include <format>

#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>

namespace wowlib
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
      : next_(start.value)
      , max_(max.value)
    {
    }

    /** Allocate the next id.
        @return the id, or FdidSpaceExhausted once max is passed. */
    Result<FileDataID> next()
    {
      if (next_ > max_)
        return make_error(ErrorCode::FdidSpaceExhausted,
                          std::format("custom FileDataID space exhausted at {}", max_));
      return FileDataID{static_cast<std::uint32_t>(next_++)};
    }

    /** Bump the allocator past an id seen in a persisted sidecar, so reloading a
        project never re-hands-out an id already in use.
        @param id an existing custom id. */
    void note_existing(FileDataID id)
    {
      if (id.value >= next_ && id.value <= max_)
        next_ = id.value + 1;
    }

    /** The id the next successful call to next() would return. */
    FileDataID peek() const { return FileDataID{static_cast<std::uint32_t>(next_)}; }

  private:
    std::uint64_t next_;   // u64 internally so next_ can pass max_ without wrapping
    std::uint64_t max_;
  };
}