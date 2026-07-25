#pragma once

/** @file
    nanobind type caster for wowlib::formats::Repeated<T, N> — the fixed-capacity
    "a chunk may appear up to N times" container (chunk.hpp; MOTV texcoord sets,
    MOCV vertex-color layers).

    welder's opaque-container generator only names std::vector, so a Repeated<>
    member would otherwise fail the bindability gate and has to be excluded. A
    self-contained caster (built with NB_TYPE_CASTER, so it does NOT derive from
    the registration-needing base) makes has_native_caster<Repeated<T,N>> true:
    welder's gate then passes automatically and the caster names the type in the
    .pyi stubs — no weld, no trust mark needed (see welder's "Trust & type casters",
    option 3). Repeated<> surfaces as a Python `list` of its filled slots, by value
    — reading copies the slots out; assigning fills fresh slots via push() (up to N,
    else the conversion is rejected).

    Include AFTER the wowlib headers and nanobind, BEFORE the welder module
    expansion (like result_casters.hpp). Only the limited C API is used, so
    stable-ABI (abi3) builds stay valid. */

#include <cstddef>
#include <cstdint>

#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>          // the inner std::vector<T> caster

#include <wowlib/formats/common/chunked_file.hpp>

namespace nanobind::detail
{
  template <typename T, std::size_t N>
  struct type_caster<wowlib::formats::Repeated<T, N>>
  {
    using RepeatedT = wowlib::formats::Repeated<T, N>;
    using Caster = make_caster<T>;

    NB_TYPE_CASTER(RepeatedT, const_name("list[") + Caster::Name + const_name("]"))

    bool from_python(handle src, uint8_t flags, cleanup_list*) noexcept
    {
      PyObject* obj = src.ptr();
      if (!PySequence_Check(obj))
        return false;
      const Py_ssize_t n = PySequence_Size(obj);
      if (n < 0)
      {
        PyErr_Clear();
        return false;
      }
      if (static_cast<std::size_t>(n) > N)
        return false;                     // more occurrences than the slot capacity
      const bool convert = (flags & static_cast<uint8_t>(cast_flags::convert)) != 0;
      value = RepeatedT{};                 // fresh: slots refill from index 0
      for (Py_ssize_t i = 0; i < n; ++i)
      {
        object item = steal(PySequence_GetItem(obj, i));
        if (!item.is_valid())
        {
          PyErr_Clear();
          return false;
        }
        T* slot = value.push();
        if (!slot || !try_cast<T>(item, *slot, convert))
          return false;
      }
      return true;
    }

    static handle from_cpp(const RepeatedT& r, rv_policy policy, cleanup_list* cleanup)
    {
      const Py_ssize_t n = static_cast<Py_ssize_t>(r.size());
      object out = steal(PyList_New(n));
      if (!out.is_valid())
        return handle();
      for (Py_ssize_t i = 0; i < n; ++i)
      {
        handle h = Caster::from_cpp(r[static_cast<std::size_t>(i)], policy, cleanup);
        if (!h.is_valid())
          return handle();
        if (PyList_SetItem(out.ptr(), i, h.ptr()) != 0)  // steals h's reference
        {
          PyErr_Clear();
          return handle();
        }
      }
      return out.release();
    }
  };
}
