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
      ClientFileSystem composes over. read_file/exists are thread-safe; open/close
      are not (initialize before sharing). Not bound — static polymorphism only.
      @tparam B the backend type under test. */
  template <typename B>
  concept StorageBackend = requires(B b, const B cb, const FileKey& key) {
    { b.open() } -> std::same_as<Result<void>>;
    { b.close() } noexcept;
    { cb.is_open() } -> std::same_as<bool>;
    { b.read_file(key) } -> std::same_as<Result<FileBuffer>>;
    { b.exists(key) } -> std::same_as<bool>;
    { B::kind() } -> std::same_as<StorageKind>;
  };
}