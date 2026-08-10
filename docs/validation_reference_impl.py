"""Generate the validation-contract reference from the C++ sources.

`validate()` checks two kinds of contract (see the guide page this fills):
declarative ones the entities carry as annotations — `count_matches`,
`indexes`, `expected_value`, … — and imperative ones a `validate_extra` hook
spells out because no annotation can express them. Only the first kind is
machine-readable, and it is the kind a user most wants listed: "what does
wowlib actually check on a WMO group?".

So this scans the headers for those annotations rather than restating them in
prose, which means the table cannot drift from the code: adding a contract to
an entity adds a row here, and deleting one deletes the row. Entities carrying
a hook are flagged, so the page never implies the annotation list is the whole
story.

The parse is deliberately shallow — it recognises the codebase's own annotation
style (a vertical `[[ … ]]` block immediately preceding the member it belongs
to) and nothing more. It never has to be a C++ parser: a mis-parse shows up as
a missing or odd row on the page, and `check()` fails the docs build if a file
that should yield contracts yields none.
"""

import os
import re

_HERE = os.path.dirname(os.path.abspath(__file__))
_SRC = os.path.normpath(os.path.join(_HERE, "..", "src", "wowlib"))

#: Where each format's entity headers live, in the order the page lists them.
#: A format with no annotated contracts is simply absent from the page.
FORMAT_ROOTS = [
    ("WMO", "formats/wmo"),
    ("M2", "formats/m2"),
    ("ADT", "formats/adt"),
    ("WDT", "formats/wdt"),
    ("WDL", "formats/wdl"),
    ("BLP", "formats/blp"),
    ("Client databases", "db"),
]

#: One renderer per validation annotation: (regex over the annotation block,
#: formatter taking the match). The wording is the CONTRACT, not the spelling —
#: a reader should not need to know the annotation vocabulary to use the page.
_CONTRACTS = [
    (
        re.compile(r"count_matches\(\s*\"(\w+)\"\s*(?:,\s*(\d+)\s*)?\)"),
        lambda m: (
            f"one per `{m.group(1)}` entry"
            if not m.group(2)
            else f"one per {m.group(2)} `{m.group(1)}` entries"
        ),
    ),
    (
        re.compile(r"count_exactly\(\s*([\w:]+)\s*\)"),
        lambda m: f"exactly `{m.group(1)}` entries",
    ),
    (
        re.compile(r"count_multiple_of\(\s*(\d+)\s*\)"),
        lambda m: f"a multiple of {m.group(1)} entries",
    ),
    (
        re.compile(r"indexes\(\s*\"(\w+)\"\s*\)"),
        lambda m: f"every value indexes `{m.group(1)}`",
    ),
    (
        re.compile(r"indexes_optional\(\s*\"(\w+)\"\s*\)"),
        lambda m: f"every value indexes `{m.group(1)}` (or is the 'none' sentinel)",
    ),
    (
        re.compile(r"indexes_in_root\(\s*\"(\w+)\"\s*\)"),
        lambda m: f"every value indexes the root's `{m.group(1)}`",
    ),
    (
        re.compile(r"expected_value\(\s*([\w:]+)\s*\)"),
        lambda m: f"always `{m.group(1)}`",
    ),
    (re.compile(r"formats::nonempty|=\s*nonempty\b"), lambda m: "must not be empty"),
]

#: `struct`/`class` introducer; the name may sit after a multi-line `[[ … ]]`.
_TYPE_RE = re.compile(r"^\s*(?:template\s*<.*>\s*)?(?:struct|class)\b(.*)$")
#: A member declaration's trailing identifier: `std::vector<T> name;`, `T name{};`.
_MEMBER_RE = re.compile(r"(\w+)\s*(?:\[\s*\d*\s*\])?\s*(?:=[^;]*|\{[^;]*\})?;\s*$")
#: A hand-written `validate()` — either declared in-class or defined out of line
#: as `ValidationReport Thing<V>::validate() const`.
_VALIDATE_RE = re.compile(r"ValidationReport\s+(?:(?:\w+::)*?(\w+)<[^>]*>::)?validate\(\)")
#: The CRTP mixins whose validate() IS the generic engine entry point, not a
#: format's own contracts; listing them would say nothing about any file.
_MIXINS = {"ChunkedFile", "M2OffsetBlock"}
#: A conditionally-inherited version-range trait in an entity's base list:
#: `slot<V, builds::Cata, GroupBodyCata>`. Its members belong to the ENTITY as
#: far as any user is concerned (welder flattens them onto the binding), so the
#: page must not expose the internal trait name.
_SLOT_RE = re.compile(r"\bslot\s*<(.+)$")


def _slot_trait(text):
    """The trait argument of a ``slot<…>`` base, or None.

    Args:
        text: everything after ``slot<`` on the line.

    Returns:
        The bare trait name (namespace qualifiers and template arguments
        stripped), or ``None`` when the base list is not a well-formed slot.
    """
    args, depth, current = [], 0, ""
    for char in text:
        if char in "<{(":
            depth += 1
        elif char in ">})":
            if char == ">" and depth == 0:
                break
            depth -= 1
        if char == "," and depth == 0:
            args.append(current)
            current = ""
            continue
        current += char
    args.append(current)
    if len(args) < 3:
        return None
    trait = args[2].strip().split("<")[0]
    return trait.split("::")[-1] or None


def _type_name(lines, index):
    """The class name introduced at ``lines[index]``.

    The codebase writes welded types as ``struct [[ …attrs… ]] Name``, with the
    attribute block spanning lines, so the name can be several lines below the
    keyword.

    Args:
        lines: the file's lines.
        index: the line the ``struct``/``class`` keyword is on.

    Returns:
        The name, or ``None`` when the line introduces no named type (a
        forward declaration's base list, an anonymous struct).
    """
    rest = _TYPE_RE.match(lines[index]).group(1)
    depth = 0
    for line in [rest] + lines[index + 1 : index + 40]:
        for token in re.finditer(r"\[\[|\]\]|\w+", line):
            text = token.group(0)
            if text == "[[":
                depth += 1
            elif text == "]]":
                depth -= 1
            elif depth == 0 and re.fullmatch(r"\w+", text):
                if text in ("final", "alignas"):
                    continue
                return text
        if depth == 0 and ("{" in line or ":" in line):
            break
    return None


def _member_name(lines, index):
    """The member declared at or just after ``lines[index]``.

    Args:
        lines: the file's lines.
        index: the line just past the member's annotation block.

    Returns:
        The member identifier, or ``None`` when no declaration follows (the
        annotation belonged to a method or a type).
    """
    for line in lines[index : index + 6]:
        if "(" in line:            # a method, not a data member
            return None
        match = _MEMBER_RE.search(line.rstrip())
        if match:
            return match.group(1)
    return None


def scan_file(path):
    """Extract every annotated contract in one header.

    Args:
        path: the header to read.

    Returns:
        A triple ``(rows, hooks, traits)``: ``rows`` is a list of
        ``(type_name, member, [contract, …])``; ``hooks`` is the set of type
        names with hand-written checks; ``traits`` maps each version-range
        trait to the entity that inherits it.
    """
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()

    rows, hooks, traits = [], set(), {}
    current = None
    index = 0
    while index < len(lines):
        line = lines[index]

        if _TYPE_RE.match(line):
            name = _type_name(lines, index)
            if name:
                current = name

        slot_match = _SLOT_RE.search(line)
        if slot_match and current:
            trait = _slot_trait(slot_match.group(1))
            if trait:
                traits[trait] = current

        if "validate_extra" in line and current:
            hooks.add(current)
        validate_match = _VALIDATE_RE.search(line)
        if validate_match:
            owner = validate_match.group(1) or current
            if owner and owner not in _MIXINS:
                hooks.add(owner)

        # an annotation block: gather it, then look at what it decorates
        if "[[" in line and "]]" not in line.split("//")[0]:
            block, cursor = [], index
            while cursor < len(lines) and len(block) < 30:
                block.append(lines[cursor])
                if "]]" in lines[cursor]:
                    break
                cursor += 1
            text = "\n".join(block)
            found = [fmt(m) for rx, fmt in _CONTRACTS for m in [rx.search(text)] if m]
            if found and current:
                member = _member_name(lines, cursor + 1)
                if member:
                    rows.append((current, member, found))
            index = cursor + 1
            continue

        index += 1
    return rows, hooks, traits


def collect():
    """Scan every format's headers.

    Returns:
        A list of ``(format_label, rows, hooks)`` in FORMAT_ROOTS order,
        omitting formats that declare no contracts at all.
    """
    out = []
    for label, rel in FORMAT_ROOTS:
        root = os.path.join(_SRC, rel)
        rows, hooks, traits = [], set(), {}
        for base, _dirs, files in os.walk(root):
            for name in sorted(files):
                if name.endswith((".hpp", ".cpp")):
                    file_rows, file_hooks, file_traits = scan_file(os.path.join(base, name))
                    rows += file_rows
                    hooks |= file_hooks
                    traits.update(file_traits)
        # a trait's members are the entity's as far as any user can tell
        rows = [(traits.get(t, t), m, c) for t, m, c in rows]
        hooks = {traits.get(h, h) for h in hooks}
        if rows or hooks:
            out.append((label, rows, hooks))
    return out


def markdown():
    """Render the whole reference.

    Returns:
        The markdown replacing the page's marker.
    """
    parts = []
    for label, rows, hooks in collect():
        parts.append(f"### {label}\n")
        if rows:
            by_type = {}
            for type_name, member, contracts in rows:
                by_type.setdefault(type_name, []).append((member, contracts))
            parts.append("| Entity | Member | Must hold |")
            parts.append("| --- | --- | --- |")
            for type_name in sorted(by_type):
                for member, contracts in by_type[type_name]:
                    parts.append(
                        f"| `{type_name}` | `{member}` | " + "; ".join(contracts) + " |"
                    )
            parts.append("")
        extra = sorted(hooks)
        if extra:
            parts.append(
                "Additional checks that no annotation can express — record-interior "
                "ranges, flag/presence coherence, cross-file references — are "
                "hand-written for "
                + ", ".join(f"`{name}`" for name in extra)
                + ".\n"
            )
    return "\n".join(parts)


def check():
    """Fail the docs build when the scan silently stops finding contracts.

    A refactor that renames the annotations, restyles the `[[ … ]]` blocks or
    moves the headers would otherwise empty this page without anyone noticing —
    the exact drift the generated page exists to prevent.

    Raises:
        RuntimeError: when a format known to carry contracts yields none.
    """
    found = {label for label, rows, _hooks in collect() if rows}
    missing = {"WMO", "M2", "ADT"} - found
    if missing:
        raise RuntimeError(
            "validation reference found no annotated contracts for: "
            + ", ".join(sorted(missing))
            + " — the annotation vocabulary or the header layout changed; "
              "update docs/validation_reference_impl.py"
        )


MARKER = "<!-- validation-contracts -->"


def on_page_markdown(markdown_text, page, config, files, **kwargs):
    """mkdocs hook: fill the contracts marker on the validation guide page."""
    if MARKER in markdown_text:
        check()
        return markdown_text.replace(MARKER, markdown())
    return markdown_text
