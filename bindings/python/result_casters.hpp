#pragma once

/** @file
    nanobind type casters that make wowlib's core vocabulary natively
    convertible, which is also what satisfies welder's bindability gate:

    - `Result<T>` (std::expected): unwraps to T on success; on failure throws
      wowlib::result_error, which the module's exception translator maps onto the
      reflection-generated hierarchy (wowlib.Error base, one class per ErrorCode).
    - `FileBuffer` (std::vector<std::byte>): converts to/from Python `bytes`.
    - `std::span<const std::byte>`: accepts `bytes` arguments (copied for the
      call's duration).

    Include AFTER the wowlib headers and nanobind, BEFORE the welder module
    expansion. Only the limited C API is used, keeping stable-ABI builds valid. */

#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <nanobind/nanobind.h>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib
{
  /** The C++ carrier a Result caster throws for the error branch; the module
      registers a translator mapping it onto the generated exception hierarchy. */
  struct result_error : std::runtime_error
  {
    explicit result_error(Error e)
      : std::runtime_error(std::string{to_string(e.code)} + ": " + e.message)
      , error(std::move(e))
    {
    }

    Error error;
  };
}

namespace nanobind::detail
{
  template <typename T>
  struct type_caster<wowlib::Result<T>>
  {
    using Caster = make_caster<T>;

    NB_TYPE_CASTER(wowlib::Result<T>, Caster::Name)

    bool from_python(handle, uint8_t, cleanup_list*) noexcept { return false; }

    static handle from_cpp(wowlib::Result<T>&& result, rv_policy policy,
                           cleanup_list* cleanup)
    {
      if (!result)
        throw wowlib::result_error(std::move(result.error()));
      return Caster::from_cpp(std::move(*result), policy, cleanup);
    }

    static handle from_cpp(const wowlib::Result<T>& result, rv_policy policy,
                           cleanup_list* cleanup)
    {
      if (!result)
        throw wowlib::result_error(result.error());
      return Caster::from_cpp(*result, policy, cleanup);
    }
  };

  template <>
  struct type_caster<wowlib::Result<void>>
  {
    NB_TYPE_CASTER(wowlib::Result<void>, const_name("None"))

    bool from_python(handle, uint8_t, cleanup_list*) noexcept { return false; }

    static handle from_cpp(const wowlib::Result<void>& result, rv_policy, cleanup_list*)
    {
      if (!result)
        throw wowlib::result_error(result.error());
      return none().release();
    }
  };

  template <>
  struct type_caster<wowlib::FileBuffer>
  {
    NB_TYPE_CASTER(wowlib::FileBuffer, const_name("bytes"))

    bool from_python(handle src, uint8_t, cleanup_list*) noexcept
    {
      char* data = nullptr;
      Py_ssize_t size = 0;
      if (PyBytes_AsStringAndSize(src.ptr(), &data, &size) != 0)
      {
        PyErr_Clear();
        return false;
      }
      value.resize(static_cast<std::size_t>(size));
      std::memcpy(value.data(), data, static_cast<std::size_t>(size));
      return true;
    }

    static handle from_cpp(const wowlib::FileBuffer& buffer, rv_policy, cleanup_list*)
    {
      return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(buffer.data()),
                                       static_cast<Py_ssize_t>(buffer.size()));
    }
  };

  template <>
  struct type_caster<std::span<const std::byte>>
  {
    // owns a copy for the call's duration; the span points into it
    wowlib::FileBuffer storage;

    NB_TYPE_CASTER(std::span<const std::byte>, const_name("bytes"))

    bool from_python(handle src, uint8_t, cleanup_list*) noexcept
    {
      char* data = nullptr;
      Py_ssize_t size = 0;
      if (PyBytes_AsStringAndSize(src.ptr(), &data, &size) != 0)
      {
        PyErr_Clear();
        return false;
      }
      storage.resize(static_cast<std::size_t>(size));
      std::memcpy(storage.data(), data, static_cast<std::size_t>(size));
      value = storage;
      return true;
    }

    static handle from_cpp(std::span<const std::byte> data, rv_policy, cleanup_list*)
    {
      return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(data.data()),
                                       static_cast<Py_ssize_t>(data.size()));
    }
  };
}