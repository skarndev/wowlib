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
    return text.replace("\\", "\\\\").replace('"', '\\"')


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
    doc = " ".join(doc_parts) or None

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


def emit_table(table: str, ranges: list[Range]) -> str:
    """The generated header for ``table`` (e.g. "Map")."""
    var = snake(table)
    grid = [t for r in ranges for t in r.targets]

    out: list[str] = []
    out.append("#pragma once")
    out.append("")
    out.append(f"// Generated by dbdgen from {table}.dbd (WoWDBDefs) — do not edit.")
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
    out.append("namespace wowlib::db::tables")
    out.append("{")
    out.append("  namespace detail")
    out.append("  {")
    out.append("    template <ClientVersion V>")
    out.append(f"    struct {table}Record;")
    for r in ranges:
        era = r.canonical.era
        out.append("")
        out.append("    template <>")
        out.append(f"    struct {table}Record<versions::{era}>")
        out.append("    {")
        out.append(f"      static constexpr ClientVersion version = versions::{era};")
        out.append(f'      static constexpr std::string_view table_name = "{table}";')
        for member in r.members:
            out.append("")
            out.append(member.declaration("      "))
        out.append("")
        out.append(f"      bool operator==(const {table}Record&) const = default;")
        out.append("    };")
    out.append("  }")
    out.append("")

    grid_list = ", ".join(f"versions::{t.era}" for t in grid)
    out.append(f"  inline constexpr std::array<ClientVersion, {len(grid)}> "
               f"{var}_grid{{{grid_list}}};")
    pivots = [r.canonical for r in ranges[1:]]
    pivot_list = ", ".join(f"versions::{t.era}" for t in pivots)
    out.append(f"  inline constexpr std::array<ClientVersion, {len(pivots)}> "
               f"{var}_pivots{{{pivot_list}}};")
    out.append("")
    out.append("  template <ClientVersion V>")
    out.append(f"  using {table}Record = detail::{table}Record<formats::canonical_version("
               f"V, {var}_pivots, {var}_grid)>;")
    out.append("")
    out.append("  template <ClientVersion V>")
    out.append(f"  using {table} = Table<{table}Record<V>>;")
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


def write_if_changed(path: Path, content: str) -> bool:
    """Write ``content`` to ``path`` unless it already matches (keeps ninja
    from rebuilding unchanged tables)."""
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return True
