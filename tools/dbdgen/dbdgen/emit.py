"""C++ emission: effective member lists per target, range collapsing, and the
per-table header / per-era manifest writers."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from dbdgen.dbd import DbdError, Definition, Entry, VersionBlock
from dbdgen.targets import Target, locstring_langs

_INT_TYPES = {
    (8, False): "std::int8_t",
    (8, True): "std::uint8_t",
    (16, False): "std::int16_t",
    (16, True): "std::uint16_t",
    (32, False): "std::int32_t",
    (32, True): "std::uint32_t",
    (64, False): "std::int64_t",
    (64, True): "std::uint64_t",
}

# C++ keywords and the record statics generated members must not shadow.
_RESERVED = {
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch",
    "char", "class", "concept", "const", "consteval", "constexpr", "constinit",
    "continue", "decltype", "default", "delete", "do", "double", "else", "enum",
    "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
    "not", "nullptr", "operator", "or", "private", "protected", "public",
    "register", "requires", "return", "short", "signed", "sizeof", "static",
    "static_assert", "struct", "switch", "template", "this", "throw", "true",
    "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "while", "xor",
    "version", "table_name",
}


def snake(name: str) -> str:
    """Mechanical CamelCase -> snake_case (acronym runs kept together)."""
    s = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    s = re.sub(r"__+", "_", s.lower())
    return s


def member_name(dbd_name: str) -> str:
    """The C++ member spelling of a DBD column name: snake_case, the
    ``_lang`` locstring marker dropped, keywords and the record statics
    escaped with a trailing underscore."""
    base = dbd_name[:-5] if dbd_name.endswith("_lang") else dbd_name
    name = snake(base)
    if name in _RESERVED:
        name += "_"
    if name[0].isdigit():
        name = "_" + name
    return name


def _escape(text: str) -> str:
    # \? because gcc still warns about trigraph-looking sequences (some WoWDBDefs
    # comments open with "??!!"); escaping every ? guarantees no ??x survives.
    return (text.replace("\\", "\\\\").replace('"', '\\"')
                .replace("?", "\\?"))


def _doc_text(text: str) -> str:
    r"""Normalize a DBD comment into safe docstring text. nanobind renders a doc as
    a triple-quoted Python string in the .pyi, so two things in a raw WoWDBDefs
    comment corrupt the whole stub: a backslash (a WoW path like World\Map\ — a
    lone or trailing one escapes the closing quote or forms a stray \n; nanobind
    even switches to a raw string, which then cannot end in a backslash), and a
    double quote adjacent to the closing delimiter (a comment ending in a quote
    makes four quotes in a row and closes the string early). Backslashes become
    forward slashes (a WoW path reads the same) and double quotes become single
    quotes; both are docstring-safe and read identically."""
    return text.replace("\\", "/").replace('"', "'").strip()


@dataclass(frozen=True)
class Member:
    """One emitted record member."""

    name: str
    cpp_type: str  # the element type; array_len wraps it in std::array
    array_len: int | None
    is_id: bool
    is_relation: bool
    noninline: bool
    doc: str | None

    @property
    def signature(self) -> tuple:
        """The layout identity range collapsing compares (docs excluded)."""
        return (self.name, self.cpp_type, self.array_len, self.is_id,
                self.is_relation, self.noninline)

    def declaration(self, indent: str) -> str:
        """The member's full C++ declaration, annotations included."""
        annotations = []
        if self.is_id:
            annotations.append("=db::id")
        if self.is_relation:
            annotations.append("=db::relation")
        if self.noninline:
            annotations.append("=db::noninline")
        if self.doc:
            annotations.append(f'=welder::doc("{_escape(self.doc)}")')

        if self.array_len is not None:
            declared = f"std::array<{self.cpp_type}, {self.array_len}> {self.name}{{}};"
        elif self.cpp_type == "float":
            declared = f"float {self.name} = 0.0f;"
        elif self.cpp_type.startswith(("std::int", "std::uint")):
            declared = f"{self.cpp_type} {self.name} = 0;"
        else:  # std::string / LocString
            declared = f"{self.cpp_type} {self.name};"

        if not annotations:
            return f"{indent}{declared}"
        if len(annotations) == 1:
            return f"{indent}[[{annotations[0]}]]\n{indent}{declared}"
        joined = f",\n{indent}  ".join(annotations)
        return f"{indent}[[\n{indent}  {joined}]]\n{indent}{declared}"


def build_members(defn: Definition, block: VersionBlock, target: Target) -> list[Member]:
    """The effective member list of ``block`` for ``target``."""
    langs = locstring_langs(target.version)

    # The _lang strip may collide with a sibling column (ItemRandomProperties
    # has both Name and Name_lang); colliding locstrings keep their suffix.
    names = [member_name(e.name) for e in block.entries]
    for i, entry in enumerate(block.entries):
        if names.count(names[i]) > 1 and entry.name.endswith("_lang"):
            kept = snake(entry.name)
            names[i] = kept + "_" if kept in _RESERVED else kept

    members: list[Member] = []
    seen: set[str] = set()
    for entry, name in zip(block.entries, names):
        decl = defn.columns.get(entry.name)
        if decl is None:
            raise DbdError(f"entry {entry.name!r} has no COLUMNS declaration")
        members.append(_member_of(entry, decl.type, decl, langs, name))
        if name in seen:
            raise DbdError(f"member name collision on {name!r}")
        seen.add(name)
    return members


def _member_of(entry: Entry, column_type: str, decl, langs: int | None,
               name: str | None = None) -> Member:
    doc_parts = []
    comment = entry.comment or decl.comment
    if comment:
        doc_parts.append(comment)
    if decl.foreign:
        doc_parts.append(f"References {decl.foreign}.")
    if not decl.verified:
        doc_parts.append("(column name unverified)")
    doc = _doc_text(" ".join(doc_parts)) or None

    if entry.bits is not None:
        if column_type not in ("int",):
            raise DbdError(f"{entry.name}: sized entry for non-int column {column_type!r}")
        cpp = _INT_TYPES.get((entry.bits, entry.unsigned))
        if cpp is None:
            raise DbdError(f"{entry.name}: unsupported integer width {entry.bits}")
    elif column_type == "float":
        cpp = "float"
    elif column_type == "string":
        cpp = "std::string"
    elif column_type == "locstring":
        if entry.array_len is not None:
            raise DbdError(f"{entry.name}: locstring arrays are not supported")
        cpp = f"LocString{langs}" if langs is not None else "std::string"
    elif column_type == "int" and entry.noninline:
        cpp = "std::uint32_t"  # ids/relations delivered by satellite blocks
    else:
        raise DbdError(f"{entry.name}: int entry without a <size>")

    return Member(
        name=name if name is not None else member_name(entry.name),
        cpp_type=cpp,
        array_len=entry.array_len,
        is_id=entry.is_id,
        is_relation=entry.is_relation,
        noninline=entry.noninline,
        doc=doc,
    )


@dataclass
class Range:
    """One emitted record specialization: consecutive targets whose effective
    member lists are identical."""

    targets: list[Target]
    members: list[Member]

    @property
    def canonical(self) -> Target:
        return self.targets[0]


def collapse(per_target: list[tuple[Target, list[Member]]]) -> list[Range]:
    """Group consecutive targets with identical member signatures."""
    ranges: list[Range] = []
    for target, members in per_target:
        signature = [m.signature for m in members]
        if ranges and [m.signature for m in ranges[-1].members] == signature:
            ranges[-1].targets.append(target)
        else:
            ranges.append(Range(targets=[target], members=members))
    return ranges


def emit_table(table: str, ranges: list[Range], dbd_name: str | None = None) -> str:
    """The generated header for ``table`` (e.g. "Map"). ``dbd_name`` is the
    WoWDBDefs (and on-disk file) spelling when it differs from the generated
    identifier (Item-sparse -> ItemSparseLegacy); it feeds table_name so the
    record keeps naming the real DBFilesClient file."""
    var = snake(table)
    dbd_name = dbd_name or table
    grid = [t for r in ranges for t in r.targets]

    out: list[str] = []
    out.append("#pragma once")
    out.append("")
    out.append(f"// Generated by dbdgen from {dbd_name}.dbd (WoWDBDefs) — do not edit.")
    out.append("")
    out.append("#include <array>")
    out.append("#include <cstdint>")
    out.append("#include <string>")
    out.append("#include <string_view>")
    out.append("")
    out.append("#include <welder/vocabulary.hpp>")
    out.append("")
    out.append("#include <wowlib/core/client_version.hpp>")
    out.append("#include <wowlib/db/annotations.hpp>")
    out.append("#include <wowlib/db/locstring.hpp>")
    out.append("#include <wowlib/db/table.hpp>")
    out.append("#include <wowlib/formats/common/version_range.hpp>")
    out.append("")
    # Welded empty supertypes for the Python/Lua binding surface. A class-template
    # instantiation is only weldable through a welded base, and each per-range
    # concrete welds via a namespace alias (in the binding shard). The bases live
    # in dedicated namespaces named by the (unique) table name, so they never
    # collide with the tables:: aliases or with a table literally named "<X>Base".
    out.append("namespace wowlib::db::rowbase")
    out.append("{")
    out.append("  struct [[")
    out.append("    =welder::weld,")
    out.append(f'    =welder::doc("One row of the {table} client-database table.")]]')
    out.append(f"  {table}")
    out.append("  {")
    out.append(f"    bool operator==(const {table}&) const = default;")
    out.append("  };")
    out.append("}")
    out.append("")
    out.append("namespace wowlib::db::tables")
    out.append("{")
    # The table supertype lives IN tables (named {table}_ to dodge the tables::
    # {table} alias and any table literally named "<X>Base"; welded AS {table}),
    # so `tables` holds directly-welded content and surfaces as a submodule —
    # a namespace of only aliases does not, and the per-range concretes weld
    # through aliases here.
    out.append("  struct [[")
    out.append("    =welder::weld,")
    out.append(f'    =welder::weld_as("{table}"),')
    out.append(f'    =welder::doc("The {table} client-database table (DBFilesClient/'
               f'{var}.db2 or .dbc).")]]')
    out.append(f"  {table}_")
    out.append("  {")
    out.append("  };")
    out.append("")
    out.append("  namespace detail")
    out.append("  {")
    out.append("    template <ClientVersion V>")
    out.append(f"    struct {table}Record;")
    for r in ranges:
        era = r.canonical.era
        out.append("")
        out.append("    template <>")
        # Own weld (besides inheriting the row supertype) so the per-range alias
        # in tables:: participates — welder welds a class-template instantiation
        # via an alias only when the target type is itself welded.
        out.append("    struct [[")
        out.append("      =welder::weld,")
        out.append(f'      =welder::doc("A {table} table row for {era}+ clients.")]]')
        out.append(f"    {table}Record<versions::{era}> : db::rowbase::{table}")
        out.append("    {")
        out.append(f"      static constexpr ClientVersion version = versions::{era};")
        out.append(f'      static constexpr std::string_view table_name = "{dbd_name}";')
        for member in r.members:
            out.append("")
            out.append(member.declaration("      "))
        out.append("")
        out.append(f"      bool operator==(const {table}Record&) const = default;")
        out.append("    };")
    out.append("  }")
    out.append("")

    # The canonicalization grid/pivots live in detail so weld_namespace does not
    # surface them as db.tables module attributes (an unannotated namespace never
    # participates in the walk); the facade reads them as detail::{var}_grid.
    grid_list = ", ".join(f"versions::{t.era}" for t in grid)
    pivots = [r.canonical for r in ranges[1:]]
    pivot_list = ", ".join(f"versions::{t.era}" for t in pivots)
    out.append("  namespace detail")
    out.append("  {")
    out.append(f"    inline constexpr std::array<ClientVersion, {len(grid)}> "
               f"{var}_grid{{{grid_list}}};")
    out.append(f"    inline constexpr std::array<ClientVersion, {len(pivots)}> "
               f"{var}_pivots{{{pivot_list}}};")
    # The per-range class-name suffixes, checked against C++ range_suffix so the
    # binding's concrete_name lookups can never drift from dbdgen's naming.
    rows = ", ".join(
        f'{{"{range_suffix(r, grid)}", versions::{r.canonical.era}}}'
        for r in ranges)
    out.append(f"    inline constexpr std::array<formats::RangeRow, {len(ranges)}> "
               f"{var}_ranges{{{{{rows}}}}};")
    out.append(f"    static_assert(formats::ranges_valid({var}_ranges, {var}_pivots, "
               f"{var}_grid));")
    out.append("  }")
    out.append("")
    out.append("  template <ClientVersion V>")
    out.append(f"  using {table}Record = detail::{table}Record<formats::canonical_version("
               f"V, detail::{var}_pivots, detail::{var}_grid)>;")
    out.append("")
    out.append("  namespace detail")
    out.append("  {")
    out.append("    template <ClientVersion V>")
    # Own weld (besides inheriting the table supertype) so the per-range alias
    # in tables:: participates in the module walk — welder binds a
    # class-template instantiation named by an alias only when the target type
    # is itself welded (welded_for checks the type's own annotations, not bases).
    out.append("    struct [[")
    out.append("      =welder::weld,")
    out.append(f'      =welder::doc("The {table} client-database table.")]]')
    out.append(f"    {table}Table : Table<{table}Record<V>>, {table}_")
    out.append("    {")
    out.append("    };")
    out.append("  }")
    out.append("")
    out.append("  template <ClientVersion V>")
    out.append(f"  using {table} = detail::{table}Table<formats::canonical_version("
               f"V, detail::{var}_pivots, detail::{var}_grid)>;")
    out.append("}")
    out.append("")
    return "\n".join(out)


def emit_manifest(era: str, tables: list[str]) -> str:
    """The per-era manifest: includes every table generated for ``era`` and an
    X-macro enumerating them."""
    out: list[str] = []
    out.append("#pragma once")
    out.append("")
    out.append(f"// Generated by dbdgen — every table generated for the {era} era. "
               "Do not edit.")
    out.append("")
    for table in tables:
        out.append(f"#include <wowlib/db/tables/{snake(table)}.hpp>")
    out.append("")
    macro = f"WOWLIB_DB_TABLES_{era.upper()}(X)"
    rows = [f"  X({table})" for table in tables]
    body = " \\\n".join(rows)
    out.append(f"#define {macro} \\\n{body}")
    out.append("")
    return "\n".join(out)


def range_suffix(rng: Range, grid: list[Target]) -> str:
    """The class-name suffix naming ``rng``'s range on ``grid`` — the Python
    twin of C++ ``wowlib::formats::range_suffix`` (version_range.hpp): the plain
    Expansion spelling for a single-era range ("Wotlk"), "FirstPlus" for a range
    reaching the grid's end ("ShadowlandsPlus"), else "FirstToLast"
    ("VanillaToTbc"). A ``Range``'s consecutive same-schema targets ARE its
    floor-range, so its first/last target give the same result as the C++ pivot
    math; the generated ``ranges_valid`` static_assert catches any drift."""
    canonical = rng.canonical
    if len(rng.targets) == 1:
        return canonical.suffix
    if rng.targets[-1].era == grid[-1].era:
        return canonical.suffix + "Plus"
    return canonical.suffix + "To" + rng.targets[-1].suffix


def emit_shard(index: int, tables: list[tuple[str, list["Range"]]]) -> str:
    """One binding shard: include this slice's table headers, declare the
    per-RANGE concrete aliases (table + record, named exactly as the facade's
    ``concrete_name`` spells them), and weld each concrete type explicitly with
    ``weld_type``.

    NOT ``weld_namespace``: ``weld_namespace<^^db::tables>`` instantiates the
    same ``bind_namespace`` specialization (identical template arguments) in
    every shard, so the instantiation ODR-merges to a single definition whose
    ``members_of`` was baked from one arbitrary TU — every shard would then bind
    that one TU's slice. ``weld_type<T>`` is keyed on the DISTINCT concrete type,
    so each specialization lives in exactly one shard and nothing merges.

    Order matters for nanobind base linkage: a table's row supertype
    (``rowbase::T``) and table supertype (``tables::T_``) weld before the
    records / concrete tables that inherit them, and the record welds before its
    table (whose ``records`` vector element is that record)."""
    out: list[str] = []
    out.append(f"// Generated by dbdgen — db binding shard {index}. Do not edit.")
    out.append("")
    out.append('#include "db_shard.hpp"')
    out.append("")
    for table, _ in tables:
        out.append(f"#include <wowlib/db/tables/{snake(table)}.hpp>")
    out.append("")
    out.append("namespace wowlib::db::tables")
    out.append("{")
    for table, ranges in tables:
        grid = [t for r in ranges for t in r.targets]
        for rng in ranges:
            suffix = range_suffix(rng, grid)
            era = rng.canonical.era
            out.append(f"  using {table}{suffix} = {table}<versions::{era}>;")
            out.append(f"  using {table}Record{suffix} = "
                       f"{table}Record<versions::{era}>;")
    out.append("}")
    out.append("")
    out.append("namespace wowlib_py::db")
    out.append("{")
    out.append(f"  void register_shard_{index}(::nanobind::module_& tables,")
    out.append(f"                              ::nanobind::module_& rowbase)")
    out.append("  {")
    out.append("    using W = ::welder::welder<::welder::rods::nanobind::rod<>,")
    out.append("                               wowlib_py::wowlib_python_naming>;")
    out.append("    namespace t = ::wowlib::db::tables;")
    out.append("    namespace rb = ::wowlib::db::rowbase;")
    for table, ranges in tables:
        grid = [t for r in ranges for t in r.targets]
        out.append("")
        out.append(f"    W::weld_type<rb::{table}>(rowbase);")
        out.append(f"    W::weld_type<t::{table}_>(tables);")
        for rng in ranges:
            suffix = range_suffix(rng, grid)
            out.append(f'    W::weld_type<t::{table}Record{suffix}>(tables, '
                       f'"{table}Record{suffix}");')
            out.append(f'    W::weld_type<t::{table}{suffix}>(tables, '
                       f'"{table}{suffix}");')
    # Facades run only after every type in this shard is registered (for_version
    # needs the base class; the AnyX union looks the concretes up by name).
    out.append("")
    for table, _ in tables:
        var = snake(table)
        out.append(f'    def_table_facade<t::{table}>(tables, "{table}", '
                   f"t::detail::{var}_pivots, t::detail::{var}_grid);")
    out.append("  }")
    out.append("}")
    out.append("")
    return "\n".join(out)


def emit_shard_registry(num_shards: int) -> str:
    """The registry header: forward-declares every ``register_shard_N`` and a
    ``register_all`` that creates the db.rowbase / db.tables submodules and fans
    out to the shards. Included by ``wowlib_module.cpp`` after the welder walk."""
    out: list[str] = []
    out.append("#pragma once")
    out.append("")
    out.append("// Generated by dbdgen — db binding-shard registry. Do not edit.")
    out.append("")
    out.append("#include <nanobind/nanobind.h>")
    out.append("")
    out.append("namespace wowlib_py::db")
    out.append("{")
    for i in range(num_shards):
        out.append(f"  void register_shard_{i}(::nanobind::module_& tables,")
        out.append(f"                          ::nanobind::module_& rowbase);")
    out.append("")
    out.append("  /** Create the db.rowbase and db.tables submodules and weld every")
    out.append("      generated table into them (one call per shard). The db submodule")
    out.append("      itself, LocString and the encrypted-section types come from the")
    out.append("      main welder walk (bindings/python/db.hpp). */")
    out.append("  inline void register_all(::nanobind::module_& module)")
    out.append("  {")
    out.append("    namespace nb = ::nanobind;")
    out.append("    nb::module_ db = nb::borrow<nb::module_>(module.attr(\"db\"));")
    out.append("    nb::module_ rowbase = db.def_submodule(")
    out.append("        \"rowbase\", \"Row supertypes, one per table.\");")
    out.append("    nb::module_ tables = db.def_submodule(")
    out.append("        \"tables\", \"The generated client-database table classes.\");")
    for i in range(num_shards):
        out.append(f"    register_shard_{i}(tables, rowbase);")
    out.append("  }")
    out.append("}")
    out.append("")
    return "\n".join(out)


def emit_stub_patterns(tables: list[tuple[str, list["Range"]]]) -> str:
    """A nanobind stubgen PATTERN_FILE spelling each table's ``AnyX`` union.

    def_table_facade binds ``AnyX`` as a runtime ``types.UnionType``; nanobind
    2.13 stubgen renders a multi-member one as the mypy-invalid subscript
    ``types.UnionType[...]``, so each entry here rewrites it to ``A | B | ...``.
    A single-range table's ``AnyX`` collapses to one class (a valid alias on its
    own), so only multi-range tables need an entry — same mechanism as the
    hand-written format patterns (stub_patterns.nb), just generated."""
    out: list[str] = []
    out.append("# Generated by dbdgen — AnyX union spellings for the db tables. "
               "Do not edit.")
    out.append("#")
    out.append("# nanobind 2.13 stubgen renders a multi-member types.UnionType as "
               "the invalid")
    out.append("# subscript types.UnionType[...]; each entry rewrites it to the "
               "correct")
    out.append("# A | B | ... alias. Single-range tables collapse to one class and "
               "need none.")
    out.append("")
    for table, ranges in sorted(tables, key=lambda e: e[0]):
        if len(ranges) < 2:
            continue
        grid = [t for r in ranges for t in r.targets]
        members = " | ".join(f"{table}{range_suffix(r, grid)}" for r in ranges)
        out.append(f"wowlib.db.tables.Any{table}$:")
        out.append(f"    Any{table} = {members}")
        out.append("")
    return "\n".join(out)


def write_if_changed(path: Path, content: str) -> bool:
    """Write ``content`` to ``path`` unless it already matches (keeps ninja
    from rebuilding unchanged tables)."""
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return True
