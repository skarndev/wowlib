/** @file
    @brief Implementation of the generic client-database ergonomics.

    See db_dyn.hpp for the surface. Lifetime model: a @c Record holds a
    non-owning pointer to its table and a @c keep_alive ties the Python row
    object to the Python table object, so the table cannot be collected while
    a row view lives; the same applies to the numpy column views (their
    owner capsule is the table object). Rows are INDEX views — deleting rows
    shifts later indices, exactly as documented on the class. */

#include "db_dyn.hpp"

#include <nanobind/eval.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/db/dyn_table.hpp>
#include <wowlib/db/schema_catalog.hpp>

#include "result_casters.hpp"

namespace wowlib_py::db
{
  namespace
  {
    using wowlib::db::Column;
    using wowlib::db::ColumnType;
    using wowlib::db::DynTable;

    /** A live row view (see the file note for the lifetime model). */
    struct Record
    {
      DynTable* table;   /**< Non-owning; keep_alive pins the Python table. */
      std::size_t index; /**< The row index this view addresses. */
    };

    /** Unwrap a Result or raise its wowlib exception. */
    template <typename T>
    decltype(auto) ok(wowlib::Result<T>&& result)
    {
      if (!result)
        throw wowlib::result_error(result.error());
      if constexpr (!std::is_void_v<T>)
        return std::move(*result);
    }

    /** The row's cell of column @a col, in the Python shape the column
        implies: scalar for scalar columns, list for arrays, list[str] for
        locale slots. */
    nb::object cell_get(const Record& r, std::size_t col, const Column& info)
    {
      DynTable& t = *r.table;
      switch (info.type)
      {
        case ColumnType::Int:
          if (info.array_len == 1)
            return nb::cast(ok(t.get_int(r.index, col)));
          else
          {
            nb::list out;
            for (std::size_t e = 0; e < info.array_len; ++e)
              out.append(nb::cast(ok(t.get_int(r.index, col, e))));
            return out;
          }
        case ColumnType::Float:
          if (info.array_len == 1)
            return nb::cast(ok(t.get_float(r.index, col)));
          else
          {
            nb::list out;
            for (std::size_t e = 0; e < info.array_len; ++e)
              out.append(nb::cast(ok(t.get_float(r.index, col, e))));
            return out;
          }
        case ColumnType::String:
          if (info.array_len == 1)
            return nb::cast(ok(t.get_string(r.index, col)));
          else
          {
            nb::list out;
            for (std::size_t e = 0; e < info.array_len; ++e)
              out.append(nb::cast(ok(t.get_string(r.index, col, e))));
            return out;
          }
        case ColumnType::LocString: {
          nb::list out;
          for (std::size_t e = 0; e < info.locale_count; ++e)
            out.append(nb::cast(ok(t.get_string(r.index, col, e))));
          return out;
        }
      }
      return nb::none();
    }

    /** Write the row's cell of column @a col from @a value (the mirror of
        @ref cell_get's shapes). */
    void cell_set(const Record& r, std::size_t col, const Column& info,
                  nb::handle value)
    {
      DynTable& t = *r.table;
      const auto elements = [&](std::size_t expected) {
        const nb::sequence seq = nb::cast<nb::sequence>(value);
        if (nb::len(seq) != expected)
          throw nb::value_error(
            std::format("column '{}' takes {} elements, got {}",
                        info.name_view(), expected, nb::len(seq))
              .c_str());
        return seq;
      };
      switch (info.type)
      {
        case ColumnType::Int:
          if (info.array_len == 1)
            ok(t.set_int(r.index, col, nb::cast<std::int64_t>(value)));
          else
          {
            const auto seq = elements(info.array_len);
            for (std::size_t e = 0; e < info.array_len; ++e)
              ok(t.set_int(r.index, col, nb::cast<std::int64_t>(seq[e]), e));
          }
          return;
        case ColumnType::Float:
          if (info.array_len == 1)
            ok(t.set_float(r.index, col, nb::cast<float>(value)));
          else
          {
            const auto seq = elements(info.array_len);
            for (std::size_t e = 0; e < info.array_len; ++e)
              ok(t.set_float(r.index, col, nb::cast<float>(seq[e]), e));
          }
          return;
        case ColumnType::String:
          if (info.array_len == 1)
            ok(t.set_string(r.index, col, nb::cast<std::string_view>(value)));
          else
          {
            const auto seq = elements(info.array_len);
            for (std::size_t e = 0; e < info.array_len; ++e)
              ok(t.set_string(r.index, col,
                              nb::cast<std::string_view>(seq[e]), e));
          }
          return;
        case ColumnType::LocString: {
          const auto seq = elements(info.locale_count);
          for (std::size_t e = 0; e < info.locale_count; ++e)
            ok(t.set_string(r.index, col, nb::cast<std::string_view>(seq[e]),
                            e));
          return;
        }
      }
    }

    /** Resolve @a name to a column index, as Python attribute protocol
        demands: AttributeError on a miss (so hasattr()/getattr() work). */
    std::size_t column_or_attribute_error(const DynTable& t,
                                          std::string_view name)
    {
      const auto col = t.column_index(name);
      if (!col)
        throw nb::attribute_error(
          std::format("table '{}' has no column '{}'", t.name(), name).c_str());
      return *col;
    }

    /** The numpy dtype of a numeric column view. */
    nb::dlpack::dtype pod_dtype(const wowlib::db::PodColumnView& view)
    {
      const auto bits = static_cast<std::uint8_t>(view.elem_bytes * 8);
      if (view.is_float)
        return nb::dlpack::dtype{
          static_cast<std::uint8_t>(nb::dlpack::dtype_code::Float), bits, 1};
      return nb::dlpack::dtype{
        static_cast<std::uint8_t>(view.is_signed ? nb::dlpack::dtype_code::Int
                                                 : nb::dlpack::dtype_code::UInt),
        bits, 1};
    }

    /** The zero-copy numpy view of numeric column @a col (rows or
        rows x elements), owner-pinned to the Python table object. */
    nb::object column_array(nb::handle table_obj, DynTable& t, std::size_t col)
    {
      const auto view = ok(t.pod_column(col));
      const std::size_t rows = t.row_count();
      std::size_t shape[2] = {rows, view.elems_per_row};
      const int ndim = view.elems_per_row > 1 ? 2 : 1;
      const nb::ndarray<nb::numpy> array{
        const_cast<std::byte*>(view.bytes.data()),
        static_cast<std::size_t>(ndim), shape, nb::borrow(table_obj),
        /*strides=*/nullptr, pod_dtype(view)};
      return nb::cast(array);
    }

    /** The whole column in Python shape: numpy for numerics, list[str] for
        strings, list of per-row locale lists for LocStrings. */
    nb::object column_get(nb::handle table_obj, DynTable& t, std::size_t col)
    {
      const Column info = ok(t.column_info(col));
      if (info.type == ColumnType::Int || info.type == ColumnType::Float)
        return column_array(table_obj, t, col);
      nb::list out;
      for (std::size_t r = 0; r < t.row_count(); ++r)
      {
        if (info.type == ColumnType::String && info.array_len == 1)
          out.append(nb::cast(ok(t.get_string(r, col))));
        else
          out.append(cell_get(Record{&t, r}, col, info));
      }
      return out;
    }
  }

  void register_dyn(nb::module_& module)
  {
    nb::module_ db = nb::cast<nb::module_>(module.attr("db"));
    const nb::handle table_cls = nb::type<DynTable>();

    // --- the Record row view ------------------------------------------------
    nb::class_<Record> record{db, "Record"};
    record.doc() =
      "A live view of one table row: columns read and write as ATTRIBUTES "
      "named after the schema (`row.map_name`), shaped by the column (scalar, "
      "list for arrays, list[str] for locale slots). Views address rows by "
      "INDEX — deleting rows shifts later views.";
    record.def_prop_ro(
      "row_index", [](const Record& r) { return r.index; },
      "The row index this view addresses.");
    record.def(
      "__getattr__",
      [](const Record& r, std::string_view name)
      {
        const std::size_t col = column_or_attribute_error(*r.table, name);
        return cell_get(r, col, ok(r.table->column_info(col)));
      },
      nb::arg("name"));
    record.def(
      "__setattr__",
      [](const Record& r, std::string_view name, nb::handle value)
      {
        const std::size_t col = column_or_attribute_error(*r.table, name);
        cell_set(r, col, ok(r.table->column_info(col)), value);
      },
      nb::arg("name"), nb::arg("value"));
    record.def("__dir__", [](const Record& r) {
      nb::list out;
      for (std::size_t c = 0; c < r.table->column_count(); ++c)
        out.append(nb::cast(ok(r.table->column_info(c)).name_view()));
      return out;
    });
    record.def("__repr__", [](const Record& r) {
      return std::format("<wowlib.db.Record {}[{}]>", r.table->name(), r.index);
    });

    // --- sequence protocol + column views on the welded Table ---------------
    nb::cpp_function(
      [](DynTable& self) { return self.row_count(); }, nb::name("__len__"),
      nb::scope(table_cls), nb::is_method());
    nb::cpp_function(
      [](nb::handle self, Py_ssize_t index)
      {
        DynTable& t = nb::cast<DynTable&>(self);
        const auto rows = static_cast<Py_ssize_t>(t.row_count());
        if (index < 0)
          index += rows;
        if (index < 0 || index >= rows)
          throw nb::index_error(
            std::format("row {} out of range ({} rows)", index, rows).c_str());
        return Record{&t, static_cast<std::size_t>(index)};
      },
      nb::name("__getitem__"), nb::scope(table_cls), nb::is_method(),
      nb::arg("index"), nb::keep_alive<0, 1>(),
      "The live row view at `index` (negative indices count from the end).");
    nb::cpp_function(
      [](nb::handle self, std::string_view name_or_index)
      {
        DynTable& t = nb::cast<DynTable&>(self);
        return column_get(self, t, ok(t.column_index(name_or_index)));
      },
      nb::name("column"), nb::scope(table_cls), nb::is_method(),
      nb::arg("name"),
      "The whole column by NAME: a zero-copy numpy view for numeric columns\n"
      "(rows, or rows x elements; exact dtype), list[str] for string columns,\n"
      "and a list of per-row locale lists for LocString columns.\n\n"
      "The numpy view aliases the table's storage: element writes are live,\n"
      "and the view must not outlive row insertion/removal.");
    nb::cpp_function(
      [](nb::handle self, std::size_t index)
      {
        DynTable& t = nb::cast<DynTable&>(self);
        return column_get(self, t, index);
      },
      nb::name("column"), nb::scope(table_cls), nb::is_method(),
      nb::arg("index"),
      "The whole column by INDEX (see the by-name overload).");

    // --- the catalog listing ------------------------------------------------
    db.def(
      "table_names",
      [](nb::handle version) -> std::vector<std::string_view>
      {
        const auto& catalog = wowlib::db::SchemaCatalog::embedded();
        std::vector<std::string_view> out;
        out.reserve(catalog.table_count());
        for (std::size_t i = 0; i < catalog.table_count(); ++i)
        {
          const std::string_view name = catalog.table_name(i);
          if (!version.is_none() &&
              !catalog.lookup(name, nb::cast<wowlib::ClientVersion>(version)))
            continue;
          out.push_back(name);
        }
        return out;
      },
      nb::arg("version") = nb::none(),
      "Every table the built-in WoWDBDefs data knows, name-sorted; pass a\n"
      "ClientVersion to keep only the tables that era defines.");

    // --- typed per-era table modules ----------------------------------------
    // wowlib.db.tables.<era>.<Table> — one submodule per targeted expansion,
    // exposing one real Table SUBCLASS per table that era defines. Classes are
    // created lazily through PEP 562 module __getattr__ (13k eager classes
    // would tax import time for nothing) and cached in the module dict; the
    // per-era .pyi stubs (dbdgen) type their rows era-accurately.

    // The era modules' constructor hook: a fresh (default-constructed)
    // instance re-opens itself in place via move assignment, which keeps the
    // subclass identity of `self` — a bound alternate constructor would
    // return a plain Table instead.
    nb::cpp_function(
      [](DynTable& self, std::string_view table, wowlib::ClientVersion version)
      { self = ok(DynTable::open(table, version)); },
      nb::name("_open_into"), nb::scope(table_cls), nb::is_method(),
      nb::arg("table"), nb::arg("version"),
      "Re-open this table in place with a schema resolved by name and client\n"
      "version (the `wowlib.db.tables.<era>` constructor hook).");

    nb::module_ tables = db.def_submodule(
      "tables",
      "Typed per-era table access: `wowlib.db.tables.<era>.<Table>()` opens "
      "an empty table with its schema resolved for that era's client — one "
      "submodule per targeted expansion, one class per table it defines.");

    // The class factory lives in Python: `type()` with a closure __init__ is
    // clearer there than through the C API, and it runs once per (era, table).
    nb::dict helper_globals;
    nb::exec(R"(
def _make_table_class(Table, name, version, module_name):
    era = module_name.rsplit(".", 1)[-1]
    def __init__(self):
        Table.__init__(self)
        self._open_into(name, version)
    __init__.__qualname__ = name + ".__init__"
    __init__.__doc__ = ("Open the empty " + name + " table, schema-resolved "
                        "for " + era + " clients.")
    return type(name, (Table,), {
        "__init__": __init__,
        "__module__": module_name,
        "__doc__": ("The " + name + " client-database table, schema-bound to "
                    + era + " clients."),
    })
)",
             helper_globals);
    const nb::object make_class = helper_globals["_make_table_class"];

    // `import wowlib.db.tables.<era>` resolves through sys.modules — an
    // extension module has no __path__ for the import machinery to search.
    nb::object sys_modules = nb::module_::import_("sys").attr("modules");
    sys_modules[nb::str("wowlib.db")] = db;
    sys_modules[nb::str("wowlib.db.tables")] = tables;

    static constexpr std::pair<const char*, wowlib::ClientVersion> kEras[] = {
      {"vanilla", wowlib::versions::vanilla},
      {"tbc", wowlib::versions::tbc},
      {"wotlk", wowlib::versions::wotlk},
      {"cata", wowlib::versions::cata},
      {"mop", wowlib::versions::mop},
      {"wod", wowlib::versions::wod},
      {"legion", wowlib::versions::legion},
      {"bfa", wowlib::versions::bfa},
      {"shadowlands", wowlib::versions::shadowlands},
      {"dragonflight", wowlib::versions::dragonflight},
      {"tww", wowlib::versions::tww},
    };
    for (const auto& [era_name, era_version] : kEras)
    {
      const std::string module_name =
        std::string("wowlib.db.tables.") + era_name;
      nb::module_ era_module = tables.def_submodule(
        era_name,
        std::format("Tables of the {} client ({}.{}.{} build {}): one Table "
                    "subclass per table this era defines, created on first "
                    "access.",
                    era_name, era_version.major, era_version.minor,
                    era_version.patch, era_version.build)
          .c_str());
      sys_modules[nb::str(module_name.c_str())] = era_module;

      era_module.attr("__getattr__") = nb::cpp_function(
        [era_module, era_version, make_class,
         module_name](std::string_view name) -> nb::object
        {
          const auto& catalog = wowlib::db::SchemaCatalog::embedded();
          if (!catalog.lookup(name, era_version))
            throw nb::attribute_error(
              std::format("module '{}' has no table '{}'", module_name, name)
                .c_str());
          const std::string table{name};
          nb::object cls = make_class(nb::type<DynTable>(), table, era_version,
                                      module_name);
          era_module.attr(table.c_str()) = cls;  // cache: next access is direct
          return cls;
        },
        nb::arg("name"));

      era_module.attr("__dir__") = nb::cpp_function(
        [era_module, era_version]() -> std::vector<std::string>
        {
          const auto module_dict =
            nb::cast<nb::dict>(era_module.attr("__dict__"));
          std::vector<std::string> out;
          for (auto [key, value] : module_dict)
            out.emplace_back(nb::cast<std::string>(nb::str(key)));
          const auto& catalog = wowlib::db::SchemaCatalog::embedded();
          for (std::size_t i = 0; i < catalog.table_count(); ++i)
          {
            const std::string_view name = catalog.table_name(i);
            if (!module_dict.contains(nb::str(name.data(), name.size())) &&
                catalog.lookup(name, era_version))
              out.emplace_back(name);
          }
          return out;
        });
    }
  }
}
