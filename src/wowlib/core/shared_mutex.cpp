#include <wowlib/core/shared_mutex.hpp>

#if defined(_WIN32) && defined(__GNUC__) && !defined(__clang__)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static_assert(sizeof(void*) == sizeof(SRWLOCK),
              "SRWLOCK must stay pointer-sized for the header's inline storage");

namespace wowlib
{
  void SharedMutex::lock() noexcept
  {
    AcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&_lock));
  }

  bool SharedMutex::try_lock() noexcept
  {
    return TryAcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&_lock)) != 0;
  }

  void SharedMutex::unlock() noexcept
  {
    ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&_lock));
  }

  void SharedMutex::lock_shared() noexcept
  {
    AcquireSRWLockShared(reinterpret_cast<PSRWLOCK>(&_lock));
  }

  bool SharedMutex::try_lock_shared() noexcept
  {
    return TryAcquireSRWLockShared(reinterpret_cast<PSRWLOCK>(&_lock)) != 0;
  }

  void SharedMutex::unlock_shared() noexcept
  {
    ReleaseSRWLockShared(reinterpret_cast<PSRWLOCK>(&_lock));
  }
}

#endif
