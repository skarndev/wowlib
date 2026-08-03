/** @file
    Reader/writer mutex used across the library.

    On MinGW-built Windows binaries libstdc++'s std::shared_mutex delegates to
    winpthreads' pthread_rwlock, whose writer exclusion is unreliable under
    reader contention — CI observed two writers inside an "exclusive" section
    losing serialized listfile appends. There the OS-native slim reader/writer
    lock (SRWLOCK, what MSVC's std::shared_mutex wraps) is used instead;
    everywhere else SharedMutex is std::shared_mutex. */
#pragma once

#include <shared_mutex>

namespace wowlib
{
#if defined(_WIN32) && defined(__GNUC__) && !defined(__clang__)
  /** SRWLOCK-backed shared mutex satisfying SharedLockable, drop-in for
      std::shared_mutex with std::unique_lock / std::shared_lock /
      std::scoped_lock. */
  class SharedMutex
  {
  public:
    SharedMutex() noexcept = default;
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;

    /** Acquires exclusive (writer) ownership, blocking until available. */
    void lock() noexcept;
    /** Attempts exclusive ownership without blocking.
        @return true when the lock was acquired. */
    bool try_lock() noexcept;
    /** Releases exclusive ownership. */
    void unlock() noexcept;

    /** Acquires shared (reader) ownership, blocking until available. */
    void lock_shared() noexcept;
    /** Attempts shared ownership without blocking.
        @return true when the lock was acquired. */
    bool try_lock_shared() noexcept;
    /** Releases shared ownership. */
    void unlock_shared() noexcept;

  private:
    // SRWLOCK is a single pointer; SRWLOCK_INIT is the null pointer. Kept as
    // void* so this header never drags in <windows.h>.
    void* _lock = nullptr;
  };
#else
  using SharedMutex = std::shared_mutex;
#endif
}
