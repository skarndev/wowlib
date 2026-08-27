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


def cpp_era(era: str) -> str:
    """The C++ versions:: constant for an era id ("vanilla" -> "Vanilla",
    "classic_era" -> "ClassicEra"). The Python-facing era id stays snake."""
    return "".join(w.capitalize() for w in era.split("_"))


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
            annotations.append("=db::Id")
        if self.is_relation:
            annotations.append("=db::Relation")
        if self.noninline:
            annotations.append("=db::Noninline")
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
    out.append("#include <vector>")
    out.append("")
    out.append("#include <welder/vocabulary.hpp>")
    out.append("")
    out.append("#include <wowlib/core/client_version.hpp>")
    out.append("#include <wowlib/db/annotations.hpp>")
    out.append("#include <wowlib/db/locstring.hpp>")
    out.append("#include <wowlib/db/record_bridge.hpp>")
    out.append("#include <wowlib/db/schema.hpp>")
    out.append("#include <wowlib/db/table_core.hpp>")
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
    # Deriving TableBase is what makes the whole method surface INHERITED: the
    # ten table verbs bind once on TableBase instead of once per (table x era)
    # class — the largest cost of the binding shards before this shape.
    out.append(f"  {table}_ : TableBase")
    out.append("  {")
    out.append("  };")
    out.append("")
    out.append("  namespace detail")
    out.append("  {")
    out.append("    template <ClientVersion V>")
    out.append(f"    struct {table}Record;")
    for r in ranges:
        era = r.canonical.era
        pascal_era = cpp_era(era)
        out.append("")
        out.append("    template <>")
        # Own weld (besides inheriting the row supertype) so the per-range alias
        # in tables:: participates — welder welds a class-template instantiation
        # via an alias only when the target type is itself welded.
        out.append("    struct [[")
        out.append("      =welder::weld,")
        out.append(f'      =welder::doc("A {table} table row for {era}+ clients.")]]')
        out.append(f"    {table}Record<versions::{pascal_era}> : db::rowbase::{table}")
        out.append("    {")
        out.append(f"      static constexpr ClientVersion Version = versions::{pascal_era};")
        out.append(f'      static constexpr std::string_view TableName = "{dbd_name}";')
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
    grid_list = ", ".join(f"versions::{cpp_era(t.era)}" for t in grid)
    pivots = [r.canonical for r in ranges[1:]]
    pivot_list = ", ".join(f"versions::{cpp_era(t.era)}" for t in pivots)
    out.append("  namespace detail")
    out.append("  {")
    out.append(f"    inline constexpr std::array<ClientVersion, {len(grid)}> "
               f"{var}_grid{{{grid_list}}};")
    out.append(f"    inline constexpr std::array<ClientVersion, {len(pivots)}> "
               f"{var}_pivots{{{pivot_list}}};")
    # The per-range class-name suffixes, checked against C++ range_suffix so the
    # binding's concrete_name lookups can never drift from dbdgen's naming.
    rows = ", ".join(
        f'{{"{range_suffix(r, grid)}", versions::{cpp_era(r.canonical.era)}}}'
        for r in ranges)
    out.append(f"    inline constexpr std::array<formats::RangeRow, {len(ranges)}> "
               f"{var}_ranges{{{{{rows}}}}};")
    out.append(f"    static_assert(formats::rangesValid({var}_ranges, {var}_pivots, "
               f"{var}_grid));")
    out.append("  }")
    out.append("")
    out.append("  template <ClientVersion V>")
    out.append(f"  using {table}Record = detail::{table}Record<formats::canonicalVersion("
               f"V, detail::{var}_pivots, detail::{var}_grid)>;")
    out.append("")
    out.append("  namespace detail")
    out.append("  {")
    out.append("    template <ClientVersion V>")
    # Own weld (besides inheriting the table supertype) so the per-range alias
    # in tables:: participates in the module walk — welder binds a
    # class-template instantiation named by an alias only when the target type
    # is itself welded (welded_for checks the type's own annotations, not bases).
    # The class contributes ONLY the typed records vector and its identity: the
    # method surface is inherited from TableBase (via the family supertype), and
    # the special members re-wire the shared TableCore at this instance's
    # vector — the one obligation a core owner carries.
    out.append("    struct [[")
    out.append("      =welder::weld,")
    out.append(f'      =welder::doc("The {table} client-database table.")]]')
    out.append(f"    {table}Table : {table}_")
    out.append("    {")
    out.append("      static constexpr ClientVersion Version = V;")
    out.append(f'      static constexpr std::string_view TableName = "{dbd_name}";')
    out.append("")
    out.append("      [[=welder::mark::exclude(welder::lang::py),")
    out.append("        =welder::mark::no_reassign,")
    out.append('        =welder::doc("The decoded records, file order. Mutate in '
               'place; write() serializes exactly this list.")]]')
    out.append(f"      std::vector<{table}Record<V>> records;")
    out.append("")
    out.append(f"      {table}Table() {{ _wire(); }}")
    out.append(f"      {table}Table(const {table}Table& o)")
    out.append(f"        : {table}_(o), records(o.records) {{ _wire(); }}")
    out.append(f"      {table}Table({table}Table&& o) noexcept")
    out.append(f"        : {table}_(std::move(o)), records(std::move(o.records)) "
               "{ _wire(); }")
    out.append(f"      {table}Table& operator=(const {table}Table& o)")
    out.append("      {")
    out.append(f"        {table}_::operator=(o);")
    out.append("        records = o.records;")
    out.append("        _wire();")
    out.append("        return *this;")
    out.append("      }")
    out.append(f"      {table}Table& operator=({table}Table&& o) noexcept")
    out.append("      {")
    out.append(f"        {table}_::operator=(std::move(o));")
    out.append("        records = std::move(o.records);")
    out.append("        _wire();")
    out.append("        return *this;")
    out.append("      }")
    out.append("")
    out.append("    private:")
    out.append("      void _wire()")
    out.append("      {")
    out.append("        static constexpr auto Schema = "
               f"db::schemaOf<{table}Record<V>>();")
    out.append("        this->_core.wire(&records, "
               f"&db::detail::RecordOpsFor<{table}Record<V>>,")
    out.append("                         db::TableInfo{Version, TableName, "
               "Schema});")
    out.append("      }")
    out.append("    };")
    out.append("  }")
    out.append("")
    out.append("  template <ClientVersion V>")
    out.append(f"  using {table} = detail::{table}Table<formats::canonicalVersion("
               f"V, detail::{var}_pivots, detail::{var}_grid)>;")
    out.append("}")
    out.append("")
    return "\n".join(out)


def emit_all_tables(tables: list[str]) -> str:
    """The umbrella: every generated table header, in one include.

    The per-era manifests cover one era each, so a consumer that wants the whole
    ClientDB surface would otherwise have to include all of them and know the era
    list. A backend that reflects a namespace needs exactly that — welder's C#
    rod walks ``wowlib`` from one translation unit, and its generator and shim
    must both see every welded table type. Language-neutral by construction: it
    is nothing but includes.
    """
    out: list[str] = []
    out.append("#pragma once")
    out.append("")
    out.append("// Generated by dbdgen — every generated table header. Do not edit.")
    out.append("//")
    out.append("// Pulling this in makes the whole of wowlib::db::tables visible in one")
    out.append("// TU, which is what a namespace-walking binding generator needs. It is")
    out.append("// deliberately heavy: prefer a single <wowlib/db/tables/foo.hpp>, or the")
    out.append("// per-era manifest, when you know what you need.")
    out.append("")
    for table in tables:
        out.append(f"#include <wowlib/db/tables/{snake(table)}.hpp>")
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


def _pascal(name: str) -> str:
    """snake_case -> PascalCase (the C# facade property spelling)."""
    return "".join(part[:1].upper() + part[1:] for part in name.split("_") if part)


# C# member names a facade row property must never shadow (inherited object
# methods plus the row view's own scaffolding).
_CS_ROW_RESERVED = {"Equals", "GetHashCode", "GetType", "ToString", "RowIndex"}


def _xml(text: str) -> str:
    """Escape doc text for a C# XML comment (CS1570 otherwise: WoWDBDefs
    comments carry <, & and quotes freely)."""
    return (text.replace("&", "&amp;").replace("<", "&lt;")
                .replace(">", "&gt;").replace('"', "&quot;"))


def emit_cs_facades(tables: list[tuple[str, list["Range"]]],
                    disk_names: dict[str, str],
                    shards: int = 16) -> list[str]:
    """The typed PURE-C# facade classes over the generic ``WoWLib.Database.Table``:
    per (table x range), a ``<Table><Suffix>Row`` struct with typed accessors
    calling the generic cell API at FIXED column indexes (the schema order is
    the blob's order is this order), and a ``<Table><Suffix>`` static opener.

    Plain C# — no per-class interop, no build-time cost beyond Roslyn on
    ordinary code (measured: the whole surface compiles in seconds). Sharded
    into ``shards`` files for tooling ergonomics, exactly like Bindings.cs."""
    per_table: list[str] = []
    for table, ranges in sorted(tables, key=lambda e: e[0]):
        grid = [t for r in ranges for t in r.targets]
        disk = disk_names.get(table, table)
        out: list[str] = []
        for rng in ranges:
            suffix = range_suffix(rng, grid)
            eras = ", ".join(t.era for t in rng.targets)
            canonical = rng.canonical.version
            row = f"{table}{suffix}Row"
            opener = f"{table}{suffix}"

            # Property names, deduplicated against reserved + one another.
            names: list[str] = []
            for member in rng.members:
                name = _pascal(member.name)
                if name in _CS_ROW_RESERVED or name in names or name == opener:
                    name += "Column"
                names.append(name)

            out.append(f"    /// <summary>Typed row view of {disk} "
                       f"({eras}). Wraps the generic table with accessors at "
                       f"this range's fixed column indexes.</summary>")
            out.append(f"    public struct {row}")
            out.append("    {")
            out.append("        private readonly Database.Table _table;")
            out.append("        private readonly ulong _row;")
            out.append(f"        public {row}(Database.Table table, ulong row)")
            out.append("        { _table = table; _row = row; }")
            out.append("        /// <summary>The row index this view "
                       "addresses.</summary>")
            out.append("        public ulong RowIndex => _row;")
            for col, (member, name) in enumerate(zip(rng.members, names)):
                if member.doc:
                    out.append(
                        f"        /// <summary>{_xml(member.doc)}</summary>")
                cpp = member.cpp_type
                if cpp.startswith("LocString"):
                    out.append(f"        public string {name}(ulong locale = 0)"
                               f" => _table.GetString(_row, {col}, locale);")
                    out.append(f"        public void Set{name}(string value, "
                               f"ulong locale = 0) => _table.SetString(_row, "
                               f"{col}, value, locale);")
                    out.append(f"        public uint {name}Flags")
                    out.append(f"        {{ get => _table.LocstringFlags(_row,"
                               f" {col}); set => _table.SetLocstringFlags("
                               f"_row, {col}, value); }}")
                    continue
                kind = ("string" if cpp == "std::string"
                        else "float" if cpp == "float" else "long")
                get = {"string": "GetString", "float": "GetFloat",
                       "long": "GetInt"}[kind]
                set_ = {"string": "SetString", "float": "SetFloat",
                        "long": "SetInt"}[kind]
                if member.array_len is not None:
                    out.append(f"        public {kind} {name}(ulong element)"
                               f" => _table.{get}(_row, {col}, element);")
                    out.append(f"        public void Set{name}(ulong element, "
                               f"{kind} value) => _table.{set_}(_row, {col}, "
                               f"value, element);")
                else:
                    out.append(f"        public {kind} {name}")
                    out.append(f"        {{ get => _table.{get}(_row, {col}, "
                               f"0); set => _table.{set_}(_row, {col}, value, "
                               f"0); }}")
            out.append("    }")
            out.append("")
            out.append(f"    /// <summary>The {disk} table for {eras} clients: the"
                       " generic table wrapped with typed row access - an"
                       " indexer, foreach, and delegating Read/Write. The"
                       " full generic API stays reachable via .Table.</summary>")
            out.append(f"    public sealed class {opener}")
            out.append("    {")
            out.append(f"        public const string TableName = \"{table}\";")
            # The flavor is spelled out: welder's aggregate constructor has no
            # optional parameters, and a schema range is always a retail release.
            out.append(f"        public static readonly ClientVersion Version"
                       f" = new ClientVersion({canonical[0]}, {canonical[1]}, "
                       f"{canonical[2]}, {canonical[3]}, ClientFlavor.Retail);")
            out.append("        /// <summary>The generic table underneath "
                       "(columns, cells, validation, encryption).</summary>")
            out.append("        public Database.Table Table { get; }")
            out.append(f"        /// <summary>Wrap an already-opened generic"
                       f" table of this range's schema.</summary>")
            out.append(f"        public {opener}(Database.Table table)"
                       " { Table = table; }")
            out.append(f"        /// <summary>Open an empty era-resolved"
                       f" table.</summary>")
            out.append(f"        public static {opener} Open() => "
                       f"new {opener}(Database.Table.Open(TableName, Version));")
            out.append("        public ulong RowCount => Table.RowCount;")
            out.append(f"        /// <summary>The typed view of one row."
                       f"</summary>")
            out.append(f"        public {row} this[ulong row] => "
                       f"new {row}(Table, row);")
            out.append("        public void Read(byte[] data) => "
                       "Table.Read(data);")
            out.append("        public void Read(Filesystem.FileSystem fs, "
                       "FileKey key) => Table.Read(fs, key);")
            out.append("        public byte[] Write(Database.EncryptedPolicy "
                       "policy = Database.EncryptedPolicy.Preserve) => "
                       "Table.Write(policy);")
            out.append("        public Enumerator GetEnumerator() => "
                       "new Enumerator(Table);")
            out.append("        /// <summary>Duck-typed foreach support "
                       "(no allocation).</summary>")
            out.append("        public struct Enumerator")
            out.append("        {")
            out.append("            private readonly Database.Table _table;")
            out.append("            private ulong _index;")
            out.append("            internal Enumerator(Database.Table table)")
            out.append("            { _table = table; _index = ulong.MaxValue; }")
            out.append("            public bool MoveNext()")
            out.append("            { _index = unchecked(_index + 1); "
                       "return _index < _table.RowCount; }")
            out.append(f"            public {row} Current => "
                       f"new {row}(_table, _index);")
            out.append("        }")
            out.append("    }")
            out.append("")
        per_table.append("\n".join(out))

    shards = max(1, min(shards, len(per_table) or 1))
    bins: list[list[str]] = [[] for _ in range(shards)]
    for i, text in enumerate(per_table):
        bins[i % shards].append(text)
    header = ("// <auto-generated> dbdgen typed facades over the generic "
              "WoWLib.Database.Table.\n// Plain C# at fixed column indexes - no "
              "interop of its own. Do not edit.\n#nullable enable\n\n"
              "namespace WoWLib.Database.Tables\n{\n")
    return [header + "\n".join(b) + "}\n" for b in bins]


_PY_TYPES = {"float": "float", "std::string": "str"}


def _py_member_type(member: Member) -> str:
    cpp = member.cpp_type
    if cpp.startswith("LocString"):
        return "list[str]"
    elem = _PY_TYPES.get(cpp, "int")
    return f"list[{elem}]" if member.array_len is not None else elem


def _era_members(ranges: list["Range"], era: str) -> list[Member] | None:
    """The member list of the range covering ``era``, or None when the table
    has no block for that era (era membership is per-range — tables skip
    middle eras, so a lo..hi span would over-claim)."""
    for rng in ranges:
        if any(t.era == era for t in rng.targets):
            return rng.members
    return None


def _record_class_name(table: str, body_names: set[str]) -> str:
    """The stub-only row class name: ``<table>Record``, underscore-suffixed
    out of the way of a real table of that name."""
    record_cls = f"{table}Record"
    while record_cls in body_names:
        record_cls += "_"
    return record_cls


def emit_py_era_stub(era: Target,
                     tables: list[tuple[str, list["Range"]]]) -> str:
    """One typed per-era stub module (``wowlib/db/tables/<era>.pyi``): a
    ``<X>Record(Record)`` with era-EXACT column annotations per table the era
    defines, and an ``<X>(Table[<X>Record])`` class matching the real Table
    subclass the runtime era module creates lazily on first attribute access.
    Stub-only typing over the ONE generic runtime engine — nothing here is
    compiled, and rows stay generic ``Record`` objects at runtime."""
    major, minor, patch, build = era.version
    lines = [
        f'"""Typed {era.era} ({major}.{minor}.{patch}, build {build}) '
        "client-database tables.",
        "",
        "Generated by dbdgen; do not edit. Each class is a real",
        "``wowlib.db.Table`` subclass created at runtime on first attribute",
        'access, its schema resolved for this era\'s client."""',
        "",
        "from wowlib.db import Record, Table",
        "",
    ]
    body_names = {name for name, _ in tables}
    for table, ranges in sorted(tables, key=lambda e: e[0]):
        members = _era_members(ranges, era.era)
        if members is None:
            continue
        record_cls = _record_class_name(table, body_names)
        lines.append(f"class {record_cls}(Record):")
        if not members:
            lines.append("    ...")
        for member in members:
            lines.append(f"    {member.name}: {_py_member_type(member)}")
        lines.append("")
        lines.append(f"class {table}(Table[{record_cls}]): ...")
        lines.append("")
    return "\n".join(lines)


def emit_py_tables_init(eras: list[Target]) -> str:
    """The ``wowlib/db/tables/__init__.pyi`` re-exporting every built era
    module, so ``tables.<era>.<X>`` references resolve for type checkers."""
    lines = [
        '"""Typed per-era client-database table access '
        "(`wowlib.db.tables.<era>.<Table>()`).",
        "",
        'Generated by dbdgen; do not edit."""',
        "",
    ]
    lines += [f"from wowlib.db.tables import {t.era} as {t.era}" for t in eras]
    lines.append("")
    return "\n".join(lines)


def emit_py_open_overloads(tables: list[tuple[str, list["Range"]]],
                           eras: list[Target]) -> str:
    """The ``Table.open`` typed-completion fragment merged into the generated
    ``wowlib/db/__init__.pyi`` (tools/merge_db_stub.py): one ``Literal``
    overload per table, returning the union of that table's per-era classes
    (a per-VERSION return type is not expressible — ``version`` is a runtime
    value — so the union narrows the table while the era modules narrow the
    era)."""
    out = ["# dbdgen Table.open overloads for wowlib/db/__init__.pyi. "
           "Do not edit."]
    for table, ranges in sorted(tables, key=lambda e: e[0]):
        union = " | ".join(f"tables.{t.era}.{table}" for t in eras
                           if _era_members(ranges, t.era) is not None)
        out.append("    @overload")
        out.append("    @staticmethod")
        out.append(f'    def open(table: Literal["{table}"], '
                   f"version: wowlib.ClientVersion) -> {union}: ...")
    out.append("")
    return "\n".join(out)


def emit_schema_blob(tables: list[tuple[str, list["Range"]]],
                     disk_names: dict[str, str]) -> bytes:
    """The compact binary schema blob: every table's per-range column lists,
    self-describing against its own era table — the RUNTIME twin of the
    generated headers, and the input to both the C++ ``SchemaCatalog`` and the
    consteval typed-record validation (``#embed``).

    Format WDBS v1, all little-endian, laid out section after section so a
    sequential parser needs no offsets table:

    | section | entry | layout |
    |---|---|---|
    | header  | 1 | ``u32 magic 'WDBS', u32 version=1, u32 table_count, u32 range_count, u32 column_count, u32 strpool_size, u8 era_count, u8[3] pad`` |
    | eras    | era_count | ``u16 major, u16 minor, u16 patch, u16 pad, u32 build`` |
    | tables  | table_count, sorted by identifier | ``u32 name_off, u32 disk_name_off, u32 first_range, u32 range_count`` |
    | ranges  | range_count | ``u32 first_column, u16 column_count, u16 era_mask`` |
    | columns | column_count | ``u32 name_off, u8 type, u8 bits, u8 flags, u8 locale_count, u16 array_len, u16 pad`` |
    | strpool | strpool_size bytes | NUL-terminated, offset 0 = "" |

    ``era_mask`` is a BITMASK over the blob's own era table (bit i = era i),
    not a lo..hi span: a table absent in a middle era yields a range whose
    target list has a hole, and a span would over-claim it. ``type`` mirrors
    ``wowlib::db::ColumnType`` (0 Int, 1 Float, 2 String, 3 LocString);
    ``flags`` is 1 signed | 2 id | 4 relation | 8 noninline. ``disk_name`` is
    the on-disk table truth (``Item-sparse``) where the identifier was renamed.
    """
    import struct

    from dbdgen.targets import TARGETS

    era_index = {t.era: i for i, t in enumerate(TARGETS)}
    if len(TARGETS) > 16:
        raise DbdError("era_mask is u16; widen it before adding a 17th target")

    pool = bytearray(b"\x00")
    interned: dict[str, int] = {"": 0}

    def intern(text: str) -> int:
        if text not in interned:
            interned[text] = len(pool)
            pool.extend(text.encode("utf-8"))
            pool.append(0)
        return interned[text]

    def column_entry(member: Member) -> bytes:
        cpp = member.cpp_type
        bits, signed, locale = 32, False, 0
        if cpp == "float":
            ctype = 1
        elif cpp == "std::string":
            ctype = 2
        elif cpp.startswith("LocString"):
            ctype = 3
            locale = int(cpp.removeprefix("LocString"))
        else:  # std::intN_t / std::uintN_t
            ctype = 0
            signed = not cpp.startswith("std::uint")
            bits = int(re.search(r"(\d+)_t$", cpp).group(1))
        flags = ((1 if signed else 0) | (2 if member.is_id else 0) |
                 (4 if member.is_relation else 0) |
                 (8 if member.noninline else 0))
        return struct.pack("<IBBBBHH", intern(member.name), ctype, bits, flags,
                           locale, member.array_len or 1, 0)

    tables_bin = bytearray()
    ranges_bin = bytearray()
    columns_bin = bytearray()
    n_ranges = 0
    n_columns = 0
    for table, ranges in sorted(tables, key=lambda e: e[0]):
        tables_bin += struct.pack("<IIII", intern(table),
                                  intern(disk_names.get(table, table)),
                                  n_ranges, len(ranges))
        for rng in ranges:
            mask = 0
            for target in rng.targets:
                mask |= 1 << era_index[target.era]
            ranges_bin += struct.pack("<IHH", n_columns, len(rng.members), mask)
            n_ranges += 1
            for member in rng.members:
                columns_bin += column_entry(member)
                n_columns += 1

    eras_bin = bytearray()
    for target in TARGETS:
        major, minor, patch, build = target.version
        eras_bin += struct.pack("<HHHHI", major, minor, patch, 0, build)

    header = struct.pack("<IIIIIIB3x", 0x53424457, 1, len(tables), n_ranges,
                         n_columns, len(pool), len(TARGETS))
    return bytes(header + eras_bin + tables_bin + ranges_bin + columns_bin +
                 pool)


def write_if_changed(path: Path, content: str) -> bool:
    """Write ``content`` to ``path`` unless it already matches (keeps ninja
    from rebuilding unchanged tables)."""
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return True


def write_bytes_if_changed(path: Path, content: bytes) -> bool:
    """The binary twin of :func:`write_if_changed` (the schema blob)."""
    if path.exists() and path.read_bytes() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return True
