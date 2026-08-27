/** @file
    @brief Implementation of the Python <-> FileBuffer byte marshalling helpers. */

#include "buffers.hpp"

#include <cstring>

namespace wowlib_py
{
  wowlib::FileBuffer toBuffer(nb::handle src)
  {
    const auto grab = [](PyObject* obj, wowlib::FileBuffer& out) -> bool
    {
      Py_buffer view;
      if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) != 0)
        return false;
      out.resize(static_cast<std::size_t>(view.len));
      if (view.len > 0)
        std::memcpy(out.data(), view.buf, static_cast<std::size_t>(view.len));
      PyBuffer_Release(&view);
      return true;
    };

    wowlib::FileBuffer buffer;
    if (grab(src.ptr(), buffer))
      return buffer;
    PyErr_Clear();

    // fall back to a binary file-like: something with a .read() returning bytes
    nb::object reader = nb::steal(PyObject_GetAttrString(src.ptr(), "read"));
    if (reader.is_valid())
    {
      nb::object data = reader();
      if (grab(data.ptr(), buffer))
        return buffer;
    }
    PyErr_Clear();
    throw nb::type_error("expected bytes, a bytes-like object, or a binary file-like");
  }

  nb::bytes toPybytes(const wowlib::FileBuffer& buffer)
  {
    return nb::bytes(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  }
}
