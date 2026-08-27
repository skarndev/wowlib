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
        throw wowlib::ResultError(result.error());
      if constexpr (!std::is_void_v<T>)
        return std::move(*result);
    }

    /** The row's cell of column @a col, in the Python shape the column
        implies: scalar for scalar columns, list for arrays, list[str] for
        locale slots. */
    nb::object cellGet(const Record& r, std::size_t col, const Column& info)
    {
      DynTable& t = *r.table;
      switch (info.type)
      {
        case ColumnType::Int:
          if (info.arrayLen == 1)
            return nb::cast(ok(t.getInt(r.index, col)));
          else
          {
            nb::list out;
            for (std::size_t e = 0; e < info.arrayLen; ++e)
              out.append(nb::cast(ok(t.getInt(r.index, col, e))));
            return out;
          }
        case ColumnType::Float:
          if (info.arrayLen == 1)
            return nb::cast(ok(t.getFloat(r.index, col)));
          else
          {
            nb::list out;
            for (std::size_t e = 0; e < info.arrayLen; ++e)
              out.append(nb::cast(ok(t.getFloat(r.index, col, e))));
            return out;
          }
        case ColumnType::String:
          if (info.arrayLen == 1)
            return nb::cast(ok(t.getString(r.index, col)));
          else
          {
            nb::list out;
            for (std::size_t e = 0; e < info.arrayLen; ++e)
              out.append(nb::cast(ok(t.getString(r.index, col, e))));
            return out;
          }
        case ColumnType::LocString: {
          nb::list out;
          for (std::size_t e = 0; e < info.localeCount; ++e)
            out.append(nb::cast(ok(t.getString(r.index, col, e))));
          return out;
        }
      }
      return nb::none();
    }

    /** Write the row's cell of column @a col from @a value (the mirror of
        @ref cellGet's shapes). */
    void cellSet(const Record& r, std::size_t col, const Column& info,
                  nb::handle value)
    {
      DynTable& t = *r.table;
      const auto elements = [&](std::size_t expected) {
        const nb::sequence seq = nb::cast<nb::sequence>(value);
        if (nb::len(seq) != expected)
          throw nb::value_error(
            std::format("column '{}' takes {} elements, got {}",
                        info.nameView(), expected, nb::len(seq))
              .c_str());
        return seq;
      };
      switch (info.type)
      {
        case ColumnType::Int:
          if (info.arrayLen == 1)
            ok(t.setInt(r.index, col, nb::cast<std::int64_t>(value)));
          else
          {
            const auto seq = elements(info.arrayLen);
            for (std::size_t e = 0; e < info.arrayLen; ++e)
              ok(t.setInt(r.index, col, nb::cast<std::int64_t>(seq[e]), e));
          }
          return;
        case ColumnType::Float:
          if (info.arrayLen == 1)
            ok(t.setFloat(r.index, col, nb::cast<float>(value)));
          else
          {
            const auto seq = elements(info.arrayLen);
            for (std::size_t e = 0; e < info.arrayLen; ++e)
              ok(t.setFloat(r.index, col, nb::cast<float>(seq[e]), e));
          }
          return;
        case ColumnType::String:
          if (info.arrayLen == 1)
            ok(t.setString(r.index, col, nb::cast<std::string_view>(value)));
          else
          {
            const auto seq = elements(info.arrayLen);
            for (std::size_t e = 0; e < info.arrayLen; ++e)
              ok(t.setString(r.index, col,
                              nb::cast<std::string_view>(seq[e]), e));
          }
          return;
        case ColumnType::LocString: {
          const auto seq = elements(info.localeCount);
          for (std::size_t e = 0; e < info.localeCount; ++e)
            ok(t.setString(r.index, col, nb::cast<std::string_view>(seq[e]),
                            e));
          return;
        }
      }
    }

    /** Resolve @a name to a column index, as Python attribute protocol
        demands: AttributeError on a miss (so hasattr()/getattr() work). */
    std::size_t columnOrAttributeError(const DynTable& t,
                                          std::string_view name)
    {
      const auto col = t.columnIndex(name);
      if (!col)
        throw nb::attribute_error(
          std::format("table '{}' has no column '{}'", t.name(), name).c_str());
      return *col;
    }

    /** The numpy dtype of a numeric column view. */
    nb::dlpack::dtype podDtype(const wowlib::db::PodColumnView& view)
    {
      const auto bits = static_cast<std::uint8_t>(view.elemBytes * 8);
      if (view.isFloat)
        return nb::dlpack::dtype{
          static_cast<std::uint8_t>(nb::dlpack::dtype_code::Float), bits, 1};
      return nb::dlpack::dtype{
        static_cast<std::uint8_t>(view.isSigned ? nb::dlpack::dtype_code::Int
                                                 : nb::dlpack::dtype_code::UInt),
        bits, 1};
    }

    /** The zero-copy numpy view of numeric column @a col (rows or
        rows x elements), owner-pinned to the Python table object. */
    nb::object columnArray(nb::handle tableObj, DynTable& t, std::size_t col)
    {
      const auto view = ok(t.podColumn(col));
      const std::size_t rows = t.rowCount();
      std::size_t shape[2] = {rows, view.elemsPerRow};
      const int ndim = view.elemsPerRow > 1 ? 2 : 1;
      const nb::ndarray<nb::numpy> array{
        const_cast<std::byte*>(view.bytes.data()),
        static_cast<std::size_t>(ndim), shape, nb::borrow(tableObj),
        /*strides=*/nullptr, podDtype(view)};
      return nb::cast(array);
    }

    /** The whole column in Python shape: numpy for numerics, list[str] for
        strings, list of per-row locale lists for LocStrings. */
    nb::object columnGet(nb::handle tableObj, DynTable& t, std::size_t col)
    {
      const Column info = ok(t.columnInfo(col));
      if (info.type == ColumnType::Int || info.type == ColumnType::Float)
        return columnArray(tableObj, t, col);
      nb::list out;
      for (std::size_t r = 0; r < t.rowCount(); ++r)
      {
        if (info.type == ColumnType::String && info.arrayLen == 1)
          out.append(nb::cast(ok(t.getString(r, col))));
        else
          out.append(cellGet(Record{&t, r}, col, info));
      }
      return out;
    }
  }

  void registerDyn(nb::module_& module)
  {
    nb::module_ db = nb::cast<nb::module_>(module.attr("db"));
    const nb::handle tableCls = nb::type<DynTable>();

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
        const std::size_t col = columnOrAttributeError(*r.table, name);
        return cellGet(r, col, ok(r.table->columnInfo(col)));
      },
      nb::arg("name"));
    record.def(
      "__setattr__",
      [](const Record& r, std::string_view name, nb::handle value)
      {
        const std::size_t col = columnOrAttributeError(*r.table, name);
        cellSet(r, col, ok(r.table->columnInfo(col)), value);
      },
      nb::arg("name"), nb::arg("value"));
    record.def("__dir__", [](const Record& r) {
      nb::list out;
      for (std::size_t c = 0; c < r.table->columnCount(); ++c)
        out.append(nb::cast(ok(r.table->columnInfo(c)).nameView()));
      return out;
    });
    record.def("__repr__", [](const Record& r) {
      return std::format("<wowlib.db.Record {}[{}]>", r.table->name(), r.index);
    });

    // --- sequence protocol + column views on the welded Table ---------------
    nb::cpp_function(
      [](DynTable& self) { return self.rowCount(); }, nb::name("__len__"),
      nb::scope(tableCls), nb::is_method());
    nb::cpp_function(
      [](nb::handle self, Py_ssize_t index)
      {
        DynTable& t = nb::cast<DynTable&>(self);
        const auto rows = static_cast<Py_ssize_t>(t.rowCount());
        if (index < 0)
          index += rows;
        if (index < 0 || index >= rows)
          throw nb::index_error(
            std::format("row {} out of range ({} rows)", index, rows).c_str());
        return Record{&t, static_cast<std::size_t>(index)};
      },
      nb::name("__getitem__"), nb::scope(tableCls), nb::is_method(),
      nb::arg("index"), nb::keep_alive<0, 1>(),
      "The live row view at `index` (negative indices count from the end).");
    nb::cpp_function(
      [](nb::handle self, std::string_view nameOrIndex)
      {
        DynTable& t = nb::cast<DynTable&>(self);
        return columnGet(self, t, ok(t.columnIndex(nameOrIndex)));
      },
      nb::name("column"), nb::scope(tableCls), nb::is_method(),
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
        return columnGet(self, t, index);
      },
      nb::name("column"), nb::scope(tableCls), nb::is_method(),
      nb::arg("index"),
      "The whole column by INDEX (see the by-name overload).");

    // --- the catalog listing ------------------------------------------------
    db.def(
      "table_names",
      [](nb::handle version) -> std::vector<std::string_view>
      {
        const auto& catalog = wowlib::db::SchemaCatalog::embedded();
        std::vector<std::string_view> out;
        out.reserve(catalog.tableCount());
        for (std::size_t i = 0; i < catalog.tableCount(); ++i)
        {
          const std::string_view name = catalog.tableName(i);
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
      nb::name("_open_into"), nb::scope(tableCls), nb::is_method(),
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
    nb::dict helperGlobals;
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
             helperGlobals);
    const nb::object makeClass = helperGlobals["_make_table_class"];

    // `import wowlib.db.tables.<era>` resolves through sys.modules — an
    // extension module has no __path__ for the import machinery to search.
    nb::object sysModules = nb::module_::import_("sys").attr("modules");
    sysModules[nb::str("wowlib.db")] = db;
    sysModules[nb::str("wowlib.db.tables")] = tables;

    static constexpr std::pair<const char*, wowlib::ClientVersion> Eras[] = {
      {"vanilla", wowlib::versions::Vanilla},
      {"tbc", wowlib::versions::Tbc},
      {"wotlk", wowlib::versions::Wotlk},
      {"cata", wowlib::versions::Cata},
      {"mop", wowlib::versions::Mop},
      {"wod", wowlib::versions::Wod},
      {"legion", wowlib::versions::Legion},
      {"bfa", wowlib::versions::Bfa},
      {"shadowlands", wowlib::versions::Shadowlands},
      {"dragonflight", wowlib::versions::Dragonflight},
      {"tww", wowlib::versions::Tww},
    };
    for (const auto& [era_name, era_version] : Eras)
    {
      const std::string moduleName =
        std::string("wowlib.db.tables.") + era_name;
      nb::module_ eraModule = tables.def_submodule(
        era_name,
        std::format("Tables of the {} client ({}.{}.{} build {}): one Table "
                    "subclass per table this era defines, created on first "
                    "access.",
                    era_name, era_version.major, era_version.minor,
                    era_version.patch, era_version.build)
          .c_str());
      sysModules[nb::str(moduleName.c_str())] = eraModule;

      eraModule.attr("__getattr__") = nb::cpp_function(
        [eraModule, era_version, makeClass,
         moduleName](std::string_view name) -> nb::object
        {
          const auto& catalog = wowlib::db::SchemaCatalog::embedded();
          if (!catalog.lookup(name, era_version))
            throw nb::attribute_error(
              std::format("module '{}' has no table '{}'", moduleName, name)
                .c_str());
          const std::string table{name};
          nb::object cls = makeClass(nb::type<DynTable>(), table, era_version,
                                      moduleName);
          eraModule.attr(table.c_str()) = cls;  // cache: next access is direct
          return cls;
        },
        nb::arg("name"));

      eraModule.attr("__dir__") = nb::cpp_function(
        [eraModule, era_version]() -> std::vector<std::string>
        {
          const auto moduleDict =
            nb::cast<nb::dict>(eraModule.attr("__dict__"));
          std::vector<std::string> out;
          for (auto [key, value] : moduleDict)
            out.emplace_back(nb::cast<std::string>(nb::str(key)));
          const auto& catalog = wowlib::db::SchemaCatalog::embedded();
          for (std::size_t i = 0; i < catalog.tableCount(); ++i)
          {
            const std::string_view name = catalog.tableName(i);
            if (!moduleDict.contains(nb::str(name.data(), name.size())) &&
                catalog.lookup(name, era_version))
              out.emplace_back(name);
          }
          return out;
        });
    }
  }
}
