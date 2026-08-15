#pragma once

/** @file
    @brief RecordVector — the ONE live, mutable Python view over every
    generated table's typed records vector.

    Before this, `Table.records` bound as a plain `std::vector<Record>` field,
    and nanobind's STL caster turned every access into a fresh Python `list`
    of COPIES — `t.records[0].id = 5` silently mutated a detached copy, the
    opposite of the by-reference contract the rest of the library keeps. The
    honest fix (opaque `bind_vector<Record>`) would have instantiated the whole
    vector-binding machinery ~4200 times, which is exactly the per-record-type
    code this binding has been shedding.

    So the view is ERASED like everything else in the db path: one non-template
    class over `{vector*, RecordOps*, element type object}`. Element access
    wraps the record IN PLACE via `nb::inst_reference` (a live reference whose
    keep-alive parent is the owning table), mutation goes through the RecordOps
    thunks, and the element's Python TYPE is runtime data — the registered
    record class handle captured when the `records` property is installed.

    Lifetime caveat (inherent to live views, numpy included): growing the
    vector (append/extend) may reallocate and invalidate previously handed-out
    element references. The owning table is kept alive by every element, so the
    failure mode of USE-after-realloc is reading stale-but-owned memory, not
    freed memory. */

#include <cstddef>
#include <string>
#include <string_view>

#include <nanobind/nanobind.h>

#include <wowlib/db/record_bridge.hpp>

#include "facade.hpp"  // persist()

namespace wowlib_py::db
{
  namespace nb = nanobind;

  /** The erased live view: one instance per `t.records` access, all types. */
  struct RecordVector
  {
    void* vec = nullptr;                          /**< The std::vector<Record>*. */
    const wowlib::db::detail::RecordOps* ops = nullptr; /**< Its access thunks. */
    nb::object elem_type;                         /**< The record class object. */
    nb::object owner;                             /**< The table (keep-alive). */

    std::size_t size() const { return ops->size(vec); }

    /** Normalize a possibly-negative index; raises IndexError out of range. */
    std::size_t checked(Py_ssize_t i) const
    {
      const auto n = static_cast<Py_ssize_t>(size());
      if (i < 0)
        i += n;
      if (i < 0 || i >= n)
        throw nb::index_error("record index out of range");
      return static_cast<std::size_t>(i);
    }

    /** The element's C++ address, after bounds handling. */
    void* at(Py_ssize_t i) const { return ops->at(vec, checked(i)); }

    /** Require @a v to be an instance of this vector's record class.
        @return its C++ record pointer. */
    const void* record_ptr(nb::handle v) const
    {
      if (!nb::isinstance(v, elem_type))
        throw nb::type_error(
          persist("expected a " +
                  nb::cast<std::string>(elem_type.attr("__name__")) + " record")
        );
      return nb::inst_ptr<void>(v);
    }
  };

  /** Its index-based iterator (RecordVector defines no sequence slot, so the
      implicit old-style iteration protocol would not engage). */
  struct RecordVectorIter
  {
    RecordVector view;
    std::size_t i = 0;
  };

  /** Bind RecordVector + its iterator, once, into wowlib.db. */
  inline void bind_record_vector(nb::module_& db)
  {
    nb::class_<RecordVector> rv(db, "RecordVector",
      "A live, mutable view of a table's records: indexing hands out the "
      "records IN PLACE (mutations persist), append/extend copy records in. "
      "Growing the vector may reallocate — re-index instead of holding "
      "element references across appends.");
    rv.def("__len__", [](const RecordVector& v) { return v.size(); });
    rv.def("__bool__", [](const RecordVector& v) { return v.size() != 0; });
    rv.def("__getitem__", [](const RecordVector& v, Py_ssize_t i) {
      return nb::inst_reference(v.elem_type, v.at(i), v.owner);
    });
    rv.def("__setitem__", [](RecordVector& v, Py_ssize_t i, nb::handle rec) {
      v.ops->assign_at(v.vec, v.checked(i), v.record_ptr(rec));
    });
    rv.def("__delitem__", [](RecordVector& v, Py_ssize_t i) {
      v.ops->erase_at(v.vec, v.checked(i));
    });
    rv.def("append", [](RecordVector& v, nb::handle rec) {
      v.ops->push_copy(v.vec, v.record_ptr(rec));
    }, nb::arg("record"), "Append a copy of the record.");
    rv.def("extend", [](RecordVector& v, nb::handle it) {
      for (nb::handle rec : it)
        v.ops->push_copy(v.vec, v.record_ptr(rec));
    }, nb::arg("records"), "Append a copy of every record of the iterable.");
    rv.def("clear", [](RecordVector& v) { v.ops->clear(v.vec); },
           "Remove every record.");
    rv.def("__iter__", [](const RecordVector& v) {
      return RecordVectorIter{v, 0};
    });

    nb::class_<RecordVectorIter>(db, "RecordVectorIter")
      .def("__iter__", [](RecordVectorIter& it) -> RecordVectorIter& { return it; })
      .def("__next__", [](RecordVectorIter& it) {
        if (it.i >= it.view.size())
          throw nb::stop_iteration();
        return nb::inst_reference(it.view.elem_type,
                                  it.view.ops->at(it.view.vec, it.i++),
                                  it.view.owner);
      });
  }

  /** Install the erased `records` property on one generated table class.

      Everything type-specific arrives as RUNTIME data: @a member (a
      two-instruction thunk — the only code that knows the class layout),
      @a ops (the record's erased access table) and @a elem_type (the record
      class object). One closure type serves all ~4200 classes.
      @param cls       the table class object.
      @param member    &static_cast<Cls*>(self)->records, erased.
      @param ops       the record type's RecordOps.
      @param elem_type the registered record class.
      @param elem_name the record class's Python spelling, for the stub
                       signature. */
  inline void def_records_view(nb::handle cls, void* (*member)(void*),
                               const wowlib::db::detail::RecordOps* ops,
                               nb::handle elem_type_h, const char* elem_name)
  {
    nb::object elem_type = nb::borrow(elem_type_h);
    const char* getter_sig =
      persist(std::string{"def records(self) -> wowlib.db.RecordVector"});
    (void)elem_name;  // reserved for a Generic[RecordVector] stub upgrade
    nb::object get_p{nb::cpp_function(
      [member, ops, elem_type](nb::handle self) {
        return RecordVector{member(nb::inst_ptr<void>(self)), ops, elem_type,
                            nb::borrow(self)};
      },
      nb::is_method(), nb::is_getter(), nb::sig(getter_sig),
      "The decoded records, file order — a live view: mutate elements in "
      "place, append/extend to add. write() serializes exactly this list.")};
    nb::object set_p{nb::cpp_function(
      [member, ops, elem_type](nb::handle self, nb::handle value) {
        RecordVector v{member(nb::inst_ptr<void>(self)), ops, elem_type,
                       nb::borrow(self)};
        v.ops->clear(v.vec);
        for (nb::handle rec : value)
          v.ops->push_copy(v.vec, v.record_ptr(rec));
      },
      nb::is_method())};
    nb::detail::property_install(cls.ptr(), "records", get_p.ptr(), set_p.ptr());
  }
}
