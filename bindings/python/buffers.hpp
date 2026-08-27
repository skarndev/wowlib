#pragma once

/** @file
    @brief Byte-buffer marshalling between Python objects and wowlib::FileBuffer.

    The format facade's buffer-based @c read()/write() overloads accept and produce
    raw bytes. These helpers bridge Python's byte sources/sinks and wowlib's
    in-memory @c FileBuffer, accepting the widest reasonable input (a bytes-like
    object *or* a binary file-like) so scripts can hand over @c bytes, a
    @c bytearray, a @c memoryview or an open @c io.BytesIO interchangeably. */

#include <nanobind/nanobind.h>

#include <wowlib/core/buffer.hpp>

namespace wowlib_py
{
  namespace nb = nanobind;

  /** @brief Copy a Python byte source into a @c FileBuffer.

      Accepts a bytes-like object (anything exposing the buffer protocol) or a
      binary file-like whose @c read() returns one.

      @param src the Python source object.
      @return a freshly allocated buffer holding a copy of the bytes.
      @throws nanobind::type_error if @p src is neither bytes-like nor a readable
              file-like. */
  wowlib::FileBuffer toBuffer(nb::handle src);

  /** @brief Wrap a @c FileBuffer's contents as an immutable Python @c bytes (copied). */
  nb::bytes toPybytes(const wowlib::FileBuffer& buffer);
}
