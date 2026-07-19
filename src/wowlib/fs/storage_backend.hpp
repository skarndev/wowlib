#pragma once

/** @file
    The storage-backend concept ClientFileSystem composes over. */

#include <concepts>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>

namespace wowlib::fs
{
  /** The storage backend contract: what MpqStorage and CascStorage implement and
      ClientFileSystem composes over. Backends are RAII: constructed open (through
      their static Result-returning factories, whose Options differ per backend)
      and closed by their destructors — the concept only spells the usage surface.
      read_file/exists are thread-safe. Not bound — static polymorphism only.
      @tparam B the backend type under test. */
  template <typename B>
  concept StorageBackend = std::movable<B> && requires(B b, const FileKey& key) {
    { b.read_file(key) } -> std::same_as<Result<FileBuffer>>;
    { b.exists(key) } -> std::same_as<bool>;
    { B::kind() } -> std::same_as<StorageKind>;
  };
}