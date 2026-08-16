"""Format-reference augmentation engine (reloadable impl behind format_reference.py).

The mkdocs hook is a thin shim (``format_reference.py``) that reloads THIS module
and the per-format config modules on every build, so edits here take effect under
``mkdocs serve`` without a restart (mkdocs otherwise caches an imported hook for
the process's life). Keep the event handlers (``on_page_markdown``/
``on_page_content``/``on_post_page``) here.

--- augment the versioned-format API pages with version + chunk metadata.

wowlib's file formats are version-parametric (one ``Entity<V>`` per expansion).
The C++ entity headers carry, per member, its ``=chunk()`` FourCC (chunked
entities), its ``=since()``/``=until()`` client versions (the single source of
truth for when a field exists — mirroring wowdev.wiki) and which wire struct it
is built from. This engine reads those and decorates the mkdocstrings-rendered
pages; everything FORMAT-SPECIFIC (which headers to parse, category taxonomies,
representative classes, wowdev anchor maps) lives in the per-format config
modules (``wmo_reference_config.py``, ``m2_reference_config.py``) as data plus
small functions — the machinery below is shared:

  * **Fields page** (one per format): the ``<!-- <fmt>-…-fields -->`` markers are
    filled with category-grouped mkdocstrings directives (one per field, rendered
    from a representative concrete class — or, for a field removed before it, the
    last version class that still has it). Onto each attribute heading it injects
    a FourCC badge (linking wowdev.wiki; chunked sides only) and an
    expansion-range badge (only when the field is version-restricted;
    all-version fields stay clean).
  * **Struct/record pages**: onto each wire-struct heading it can inject which
    entity field(s) use it (``[materials]``, linking back to the fields page)
    plus that field's FourCC; wire integer fields are re-annotated with their
    C++ width (``int`` → ``Annotated[int, uint32]``).
  * **Everywhere**: opaque ``Vector*`` container annotations are coerced to
    ``list[element]``, and version-suffixed class names are displayed
    generically (``WMORootTheWarWithin`` → ``WMORoot⟨version⟩``) on the pages a
    format nominates — ids/anchors keep the real name so links resolve.

Rendering rides on mkdocstrings' own heading markup, so the badges inherit the
exact class style. The expansion badges use icons from
``content/assets/expansions/<key>.{svg,png,webp}`` when present, else colored
text pills. Fail-safe: parse errors leave the page untouched.

(The badge/pill CSS class names keep their historical ``wmo-``/``exp-`` prefixes
— they are style names shared by every format, not WMO markup.)
"""

from __future__ import annotations

import html
import importlib
import json
import re
import urllib.parse
from dataclasses import dataclass, field as dc_field
from pathlib import Path
from typing import Callable

DOCS_DIR = Path(__file__).resolve().parent
REPO_ROOT = DOCS_DIR.parent
STUBS = REPO_ROOT / "build/bindings/bindings/python/stubs"

# The config modules registered with the engine, in elem-link priority order
# (an element class documented by two formats links to the first). The trailing
# "common" module carries width-only pages (the shared primitive/core types).
FORMAT_MODULES = ("wmo_reference_config", "m2_reference_config",
                  "wdt_reference_config", "wdl_reference_config",
                  "adt_reference_config",
                  "common_reference_config")

# Version-RANGE suffixes (welded class names): the library instantiates one
# class per content-permutation range, so a suffix is a plain expansion name
# ("Wotlk"), an interior range ("CataToMop") or an open-ended one
# ("LegionPlus"). Stripped from the *displayed* class name so the layout reads
# generically (WMOGroupBodyDragonflightPlus -> WMOGroupBody⟨version⟩);
# ids/anchors keep the real name so links resolve.
EXP_SUFFIXES = ("Vanilla", "Tbc", "Wotlk", "Cata", "Mop", "Wod", "Legion", "Bfa",
                "Shadowlands", "Dragonflight", "TheWarWithin")
_EXP_ALT = "(?:" + "|".join(EXP_SUFFIXES) + ")"
_SUFFIX_ALT = f"(?:{_EXP_ALT}(?:To{_EXP_ALT}|Plus)?)"
_RANGE_RE = re.compile(f"({_EXP_ALT})(?:To({_EXP_ALT})|(Plus))?$")
_VERSION_PLACEHOLDER = "⟨version⟩"
_VALUE_PLACEHOLDER = "⟨value⟩"


def split_range_suffix(name: str) -> tuple[str, str] | None:
    """``(stem, suffix)`` when *name* ends in a range suffix, else ``None``
    (the whole-name-is-a-suffix case is not a versioned class)."""
    m = _RANGE_RE.search(name)
    if not m or m.start() == 0:
        return None
    return name[: m.start()], name[m.start():]


def range_rank(suffix: str) -> int:
    """Order range suffixes by their FIRST covered expansion."""
    m = _RANGE_RE.match(suffix)
    return EXP_SUFFIXES.index(m.group(1)) if m else -1


def _generic_name(name: str) -> str:
    """Strip a trailing range suffix, replacing it with ⟨version⟩."""
    split = split_range_suffix(name)
    return split[0] + _VERSION_PLACEHOLDER if split else name


# major client version -> (css/icon key, short label, full name). Colours live in
# the stylesheet (.exp-<key>); latest supported = TheWarWithin.
EXPANSIONS = {
    1: ("vanilla", "Vanilla", "Classic"),
    2: ("tbc", "TBC", "The Burning Crusade"),
    3: ("wotlk", "WotLK", "Wrath of the Lich King"),
    4: ("cata", "Cata", "Cataclysm"),
    5: ("mop", "MoP", "Mists of Pandaria"),
    6: ("wod", "WoD", "Warlords of Draenor"),
    7: ("legion", "Legion", "Legion"),
    8: ("bfa", "BfA", "Battle for Azeroth"),
    9: ("shadowlands", "SL", "Shadowlands"),
    10: ("dragonflight", "DF", "Dragonflight"),
    11: ("tww", "TWW", "The War Within"),
}
LATEST_MAJOR = 11

# Expansion icons the user has dropped into content/assets/expansions/. Scanned
# once; absent -> the text pills are used (Blizzard's expansion logos are not
# shipped here — see that folder's README).
_ICON_DIR = DOCS_DIR / "content" / "assets" / "expansions"


def _scan_icons() -> dict[str, str]:
    found: dict[str, str] = {}
    if _ICON_DIR.is_dir():
        for _major, (key, _short, _name) in EXPANSIONS.items():
            for ext in ("svg", "png", "webp"):
                if (_ICON_DIR / f"{key}.{ext}").is_file():
                    found[key] = f"assets/expansions/{key}.{ext}"
                    break
    return found


_ICON_REL = _scan_icons()


# --- config data model --------------------------------------------------------
@dataclass
class Side:
    """One field surface of a format's fields page: an entity family whose
    members are documented as a category-grouped section (WMO root/group; the
    M2 MD20 body, the M2 chunked shell). ``kind`` is documentation-only:
    "chunked" sides carry per-member FourCC badges, "offset" sides are plain
    field listings ordered by the entity's wire_order."""
    key: str                                  # "root" | "group" | "body" | "shell"
    kind: str                                 # "chunked" | "offset"
    marker: str                               # fields-page marker this side fills
    module: str                               # python module of the entity classes
    badge_classes: tuple[str, ...]            # class prefixes whose attr headings get badges
    width_classes: tuple[str, ...]            # class prefixes whose attr sigs get int widths
    class_prefix: str                         # per-version class prefix (stub introspection)
    repr_class: str                           # the representative concrete class
    stub: str                                 # stub file (rel to STUBS) with the version classes
    parse_fields: Callable[[], dict[str, dict]]
    categorize: Callable[[dict], str | None]  # field -> category (None -> warn + first)
    category_order: tuple[str, ...]
    category_blurbs: dict[str, str]
    anchor: str                               # category-anchor prefix ("wmo-root")
    sort_key: Callable[[dict], object] | None = None   # None -> declaration order
    page: str | None = None                   # src_uri of the page this side lives on
                                              # (None -> the format's fields_page)
    _fields_cache: dict | None = dc_field(default=None, repr=False)

    def fields(self) -> dict[str, dict]:
        if self._fields_cache is None:
            self._fields_cache = self.parse_fields()
        return self._fields_cache


@dataclass
class StructPage:
    """A page of mkdocstrings-dumped wire structs/records: gets int-width
    re-annotation from its C++ headers, optional entity-field backlinks
    (``owner_side``) and its classes feed the Vector-element link registry."""
    page: str                                 # src_uri, e.g. "python/wmo/root-chunks.md"
    module: str                               # python module documented there
    stub: str                                 # stub file (rel to STUBS) with the class names
    headers: tuple[Path, ...] = ()            # C++ headers to parse int widths from
    struct_alias: dict[str, str] = dc_field(default_factory=dict)  # rendered -> C++ name
    owner_side: Side | None = None            # entity side whose fields backlink here
    enum_chunk: dict[str, tuple[str, str]] = dc_field(default_factory=dict)
    names_filter: Callable[[str], bool] | None = None  # restrict stub classes considered
    dedup_marker: str | None = None           # page marker for the deduplicated
                                              # family listing (None -> the page
                                              # dumps the module itself)
    value_templates: dict[str, set[str]] = dc_field(default_factory=dict)  # base ->
                                              # value member names (M2Track: {values})
    _classes: set[str] | None = dc_field(default=None, repr=False)
    _ints: dict | None = dc_field(default=None, repr=False)
    _reps: dict | None = dc_field(default=None, repr=False)

    def classes(self) -> set[str]:
        if self._classes is None:
            names = _stub_classes(self.stub)
            if self.names_filter:
                names = {n for n in names if self.names_filter(n)}
            self._classes = names
        return self._classes

    def int_fields(self) -> dict[str, dict[str, tuple[bool, int]]]:
        if self._ints is None:
            self._ints = _struct_int_fields(self.headers)
        return self._ints

    def anchor_class(self, name: str) -> str:
        """The class a link to ``name`` should anchor at: on a deduplicated page
        only one heading per family renders, so a versioned sibling links to its
        family's anchor (the welded base when the family has one, else the latest
        representative); elsewhere the class itself."""
        if not self.dedup_marker:
            return name
        if self._reps is None:
            self._reps = {}
            for fam in _family_groups(self).values():
                anchor = _family_anchor(fam)
                for _rank, cls in fam:
                    self._reps[cls] = anchor
        return self._reps.get(name, name)


@dataclass
class Format:
    """One versioned format's documentation surface. ``forver`` lists, per page,
    the family base classes whose ``for_version`` griffe drops (nanobind stubgen
    emits it all-@overload with no primary, so mkdocstrings renders nothing) —
    a proper method entry is injected for each as (heading id, class name,
    example expansion suffix)."""
    key: str                                  # "wmo" | "m2"
    fields_page: str                          # src_uri of the generated fields page
    legend_marker: str
    sides: tuple[Side, ...]
    wowdev_page: str                          # wiki page the FourCC badges link to
    anchors_file: str                         # vendored FourCC->anchor map in docs/
    name_re: str                              # class-name prefix regex for genericization
    generic_pages: frozenset[str]             # pages whose class names go ⟨version⟩-generic
    forver: dict[str, tuple[tuple[str, str, str], ...]]
    struct_pages: tuple[StructPage, ...]
    elem_links: dict[str, tuple[str, str]] = dc_field(default_factory=dict)
    value_alias: dict[str, str] = dc_field(default_factory=dict)  # welded value
                                              # suffix -> Base[Value] display token
    _anchors: dict | None = dc_field(default=None, repr=False)
    _res: dict | None = dc_field(default=None, repr=False)

    def anchors(self) -> dict[str, str]:
        if self._anchors is None:
            try:
                self._anchors = json.loads(
                    (DOCS_DIR / self.anchors_file).read_text(encoding="utf-8"))
            except OSError:
                self._anchors = {}
        return self._anchors

    # The genericization regexes, built once from name_re. The name where
    # mkdocstrings echoes it: the object-name heading span, the signature code
    # block (pygments name span) and cross-reference link text. Ids/hrefs are
    # untouched (the suffix must abut the closing tag), so links still resolve;
    # only the shown text goes generic. Hand-written prose uses <code>…</code>
    # and is deliberately left alone.
    def res(self) -> dict[str, re.Pattern]:
        if self._res is None:
            n = self.name_re
            self._res = {
                "classname": re.compile(
                    r'(<span class="doc doc-object-name doc-class-name">)(' + n + r')'
                    + _SUFFIX_ALT + r'(</span>)'),
                "sig": re.compile(r'(<span class="n[cf]">)(' + n + r')'
                                  + _SUFFIX_ALT + r'(</span>)'),
                "xref": re.compile(r'(<a\b[^>]*>)(' + n + r')' + _SUFFIX_ALT + r'(</a>)'),
                "toc": re.compile(r'\b(' + n + r')' + _SUFFIX_ALT + r'\b'),
            }
        return self._res


_FORMATS: list[Format] | None = None


def formats() -> list[Format]:
    """The registered format configs. Imported lazily so the engine can load
    before the configs (they import the engine for the dataclasses/parsers);
    the serve shim reloads engine + configs each build, resetting this cache."""
    global _FORMATS
    if _FORMATS is None:
        _FORMATS = [importlib.import_module(m).FORMAT for m in FORMAT_MODULES]
    return _FORMATS


def _page_url(src_uri: str) -> str:
    """The built URL path of a page src_uri (python/wmo/fields.md ->
    python/wmo/fields/), matching mkdocs' directory-URL scheme."""
    return src_uri[: -len(".md")] + "/" if src_uri.endswith(".md") else src_uri


def _side_page(fmt: Format, side: Side) -> str:
    """The page a side's field sections live on: its own ``page`` when set
    (M2 splits root/chunks onto separate pages), else the format's shared
    fields page (WMO)."""
    return side.page or fmt.fields_page


def _side_pages(fmt: Format) -> set[str]:
    return {_side_page(fmt, side) for side in fmt.sides}


# --- source parsing -----------------------------------------------------------
_CV = r"ClientVersion\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\d+\s*\}"
_BLOCK = re.compile(r"\[\[(?P<attrs>.*?)\]\]\s*(?P<decl>[^;]*?);", re.DOTALL)


def slice_text(text: str, start: str, end: str | None) -> str:
    """The region of ``text`` from the first occurrence of ``start`` to ``end``
    (or end-of-text); empty when ``start`` is absent."""
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i) if end else len(text)
    return text[i: j if j > 0 else len(text)]


def parse_version_constants(*paths: Path) -> dict[str, tuple[int, int, int]]:
    """Named ``inline constexpr ClientVersion`` constants -> (major, minor,
    patch), so annotations may reference them instead of literals. Two forms:
    brace literals (core/client_builds.hpp's ``builds::`` vocabulary) and
    aliases of an earlier name (``... m2_chunked_container = builds::Legion_Alpha;``)
    — pass client_builds.hpp BEFORE a boundaries header so its aliases
    resolve."""
    out: dict[str, tuple[int, int, int]] = {}
    for path in paths:
        text = path.read_text(encoding="utf-8")
        for m in re.finditer(
                r"inline\s+constexpr\s+ClientVersion\s+(\w+)\s*"
                r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
                text):
            out[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
        for m in re.finditer(
                r"inline\s+constexpr\s+ClientVersion\s+(\w+)\s*=\s*"
                r"(?:\w+::)*(\w+);", text):
            if m.group(2) in out:
                out[m.group(1)] = out[m.group(2)]
    return out


def _elem_name(decl: str, name: str) -> str:
    """The wire-struct/element type identifier of a member (std::vector<E> -> E,
    Repeated<std::vector<E>,N> -> E, SMOHeader -> SMOHeader), namespace-stripped."""
    t = decl.split("=")[0]
    t = re.sub(r"\{[^}]*\}", "", t)
    if name in t:
        t = t[: t.rfind(name)]
    t = t.strip().rstrip(";").strip()
    t = re.sub(r"<\s*V\s*>", "", t)
    m = re.match(r"std::vector<\s*(.+?)\s*>$", t) or \
        re.match(r"Repeated<\s*std::vector<\s*(.+?)\s*>\s*,.*>$", t)
    if m:
        t = m.group(1)
    t = re.sub(r"<[^>]*>", "", t)
    return t.split("::")[-1].strip()


def _decl_int_width(decl: str, name: str) -> tuple[bool, int] | None:
    """(unsigned, bits) of the fixed-width integer LEAF of a member declaration
    — the scalar itself, or the element of a vector / array / vector-of-array
    (mver -> uint32, TXAC's vector<array<uint8, 2>> -> uint8) — else None (a
    struct/float/string element carries no wire width to show)."""
    t = decl.split("=")[0]
    if name in t:
        t = t[: t.rfind(name)]
    m = re.search(r"\b(u?)int(8|16|32|64)_t\b", t)
    if not m:
        return None
    # Reject a type where the int is only a template argument of a record
    # (FBlock<std::uint16_t>): the leaf must be reached through vector/array only.
    outer = re.sub(r"\b(?:std|vector|array)\b|[<>,:\s]|\d+", "", t[: m.start()])
    return (m.group(1) == "u", int(m.group(2))) if outer == "" else None


def _version_ref(attrs: str, kind: str,
                 consts: dict[str, tuple] | None) -> tuple | None:
    """The =since()/=until() version in an annotation block: a ClientVersion
    literal, or a named boundary constant resolved through ``consts``."""
    m = re.search(kind + r"\(\s*" + _CV, attrs)
    if m:
        return tuple(int(x) for x in m.groups())
    m = re.search(kind + r"\(\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)\s*\)", attrs)
    if m and consts and m.group(1) in consts:
        return consts[m.group(1)]
    return None


def parse_members(text: str, consts: dict[str, tuple] | None = None,
                  header_cc: str = "") -> list[dict]:
    """Every annotated member declaration in ``text``: name, chunk FourCC,
    since/until versions, element type, container flag. ``header_cc`` is the
    FourCC credited to a ``formats::header`` prelude member (WMO's MOGP)."""
    out = []
    for m in _BLOCK.finditer(text):
        attrs, decl = m.group("attrs"), m.group("decl")
        if "welder::mark::exclude" in attrs:
            continue
        cc_m = re.search(r'chunk\("([^"]+)"', attrs)
        is_header = "formats::header" in attrs
        toks = re.findall(r"[A-Za-z_]\w*", re.sub(r"\{[^}]*\}", "", decl.split("=")[0]))
        if not toks:
            continue
        after_m = re.search(r'offset_after\("([^"]+)"\)', attrs)
        out.append({
            "name": toks[-1],
            "cc": cc_m.group(1) if cc_m else (header_cc if is_header else ""),
            "since": _version_ref(attrs, "since", consts),
            "until": _version_ref(attrs, "until", consts),
            "elem": _elem_name(decl, name=toks[-1]),
            "int_width": _decl_int_width(decl, name=toks[-1]),
            "container": "formats::container" in attrs,
            "after": after_m.group(1) if after_m else None,
        })
    return out


# --- stub introspection -------------------------------------------------------
_STUB_TEXT_CACHE: dict[str, str] = {}


def _stub_text(rel: str) -> str:
    if rel not in _STUB_TEXT_CACHE:
        try:
            _STUB_TEXT_CACHE[rel] = (STUBS / rel).read_text(encoding="utf-8")
        except OSError:
            _STUB_TEXT_CACHE[rel] = ""
    return _STUB_TEXT_CACHE[rel]


def _stub_classes(rel: str) -> set[str]:
    return set(re.findall(r"^class ([A-Za-z0-9_]+)", _stub_text(rel), re.M))


def _stub_class_blocks(rel: str) -> list[tuple[str, str]]:
    """The stub's classes as ordered (name, class block text) pairs."""
    out: list[tuple[str, str]] = []
    for block in re.split(r"(?=^class )", _stub_text(rel), flags=re.M):
        m = re.match(r"class ([A-Za-z0-9_]+)", block)
        if m:
            out.append((m.group(1), block))
    return out


def _family_anchor(fam: list[tuple[int, str]]) -> str:
    """The class a deduplicated family renders (and is linked) under: the welded
    family BASE (rank -1, a generic name like WMOBatch) when the family also has
    versioned members, otherwise the latest representative (M2 records have no
    base, so they anchor at their newest concrete version)."""
    bases = [n for r, n in fam if r < 0]
    versioned = [n for r, n in fam if r >= 0]
    return bases[0] if bases and versioned else fam[-1][1]


def _family_groups(sp: StructPage) -> dict[str, list[tuple[int, str]]]:
    """The page's classes grouped into version families: {stem: [(rank, name),
    …]} in stub order, each family sorted by its range's first expansion
    (unversioned classes are single-member families at rank -1)."""
    groups: dict[str, list[tuple[int, str]]] = {}
    for name, _block in _stub_class_blocks(sp.stub):
        if sp.names_filter and not sp.names_filter(name):
            continue
        split = split_range_suffix(name)
        stem, rank = (split[0], range_rank(split[1])) if split else (name, -1)
        groups.setdefault(stem, []).append((rank, name))
    for fam in groups.values():
        fam.sort()
    return groups


_PROP_RE = re.compile(
    r'@property\s+def (\w+)\(self\)\s*->\s*([^:\n]+):(?:\s*"""(.*?)""")?', re.S)


def _class_props(block: str) -> dict[str, tuple[str, str]]:
    """{property name: (return annotation, docstring)} of a stub class block,
    in declaration order."""
    out: dict[str, tuple[str, str]] = {}
    for m in _PROP_RE.finditer(block):
        out.setdefault(m.group(1), (m.group(2).strip(),
                                    re.sub(r"\s+", " ", m.group(3) or "").strip()))
    return out


# The opaque Vector wrappers (VectorUnsignedInt, VectorC3Vector, VectorSMOMaterial,
# VectorSMOBatchWotlk, …) mirror the list interface with the same guarantees, but
# their generated names read poorly in the docs. Coerce every Vector type annotation
# to list[element]. The element (its true Python name, incl. versioned ones like
# WMOBatchWotlk) comes from the stub's append() signature; a representative-version
# suffix is shown generically (WMOBatchWotlk -> WMOBatch⟨version⟩) to match the page.
_VECTOR_ELEMS_CACHE: dict[str, str] | None = None


def _vector_elements() -> dict[str, str]:
    global _VECTOR_ELEMS_CACHE
    if _VECTOR_ELEMS_CACHE is not None:
        return _VECTOR_ELEMS_CACHE
    out: dict[str, str] = {}
    txt = _stub_text("wowlib/__init__.pyi")
    for block in re.split(r"(?=^class )", txt, flags=re.M):
        m = re.match(r"class ((?:Vector|Array)[A-Za-z0-9_]+)[:(]", block)
        if not m:
            continue
        em = (re.search(r"def append\(self, arg:\s*([A-Za-z0-9_.]+)", block)
              or re.search(r"def __getitem__\(self, arg: int[^)]*\)\s*->\s*([A-Za-z0-9_.]+)", block))
        if not em:
            continue
        # The CONCRETE element name (incl. versioned ones like WMOBatchWotlk) —
        # the display goes generic at rewrite time, but the concrete name is what
        # the element registry and anchors are keyed by.
        out[m.group(1)] = em.group(1).split(".")[-1]
    _VECTOR_ELEMS_CACHE = out
    return out


# In the rendered HTML a Vector type annotation is either a resolved cross-ref link
# (documented vectors) or an unresolved autoref title-span (the rest). Rewrite both.
_VEC_LINK_RE = re.compile(r'<a\b[^>]*>((?:Vector|Array)[A-Za-z0-9_]+)</a>')
_VEC_SPAN_RE = re.compile(r'<span title="[^"]*\b((?:Vector|Array)[A-Za-z0-9_]+)">\1</span>')

# Element class name -> (page src_uri, module, anchor class) for linking inside
# list[…]: explicit per-format elem_links first (anchored at the name itself —
# the family bases), then every struct page's stub classes (first format/page
# wins; on a deduplicated page a versioned class anchors at its family's
# rendered representative). Lazy so it runs after the configs load; reset per
# build when the module reloads.
_ELEM_PAGE_CACHE: dict[str, tuple[str, str, str]] | None = None


def _elem_pages() -> dict[str, tuple[str, str, str]]:
    global _ELEM_PAGE_CACHE
    if _ELEM_PAGE_CACHE is None:
        _ELEM_PAGE_CACHE = {}
        for fmt in formats():
            for name, (page, module) in fmt.elem_links.items():
                _ELEM_PAGE_CACHE.setdefault(name, (page, module, name))
            for sp in fmt.struct_pages:
                for n in sp.classes():
                    _ELEM_PAGE_CACHE.setdefault(n, (sp.page, sp.module,
                                                    sp.anchor_class(n)))
    return _ELEM_PAGE_CACHE


_COMMON_CACHE: set[str] | None = None


def _common_names() -> set[str]:
    """Struct names documented on the Common structures page (from the stub)."""
    global _COMMON_CACHE
    if _COMMON_CACHE is None:
        _COMMON_CACHE = _stub_classes("wowlib/formats/common.pyi")
    return _COMMON_CACHE


def _elem_html(concrete: str, base: str) -> str:
    """The element inside list[…], displayed generically for versioned names
    (WMOBatchWotlk -> WMOBatch⟨version⟩) and linked to its documentation when it
    is a known struct: a wire struct/record on its format's struct page —
    anchored at the CONCRETE versioned class (page ids keep real names), or at
    the family representative on a deduplicated page —, an entity family by its
    unsuffixed base, or a shared primitive on the Common page (C3Vector, …).
    An element that is itself an opaque Vector (nested per-sequence containers:
    VectorVectorUnsignedInt -> VectorUnsignedInt) recurses to list[list[…]]."""
    inner = _vector_elements().get(concrete)
    if inner is not None:
        return f"list[{_elem_html(inner, base)}]"
    display = _generic_name(concrete)
    stem = display.replace(_VERSION_PLACEHOLDER, "")
    pages = _elem_pages()
    target = pages.get(concrete) or pages.get(stem)
    if target:
        page, module, anchor_cls = target
        anchor = f"{base}{_page_url(page)}#{module}.{anchor_cls}"
    elif concrete in _common_names():
        anchor = f"{base}python/common/#wowlib.formats.common.{concrete}"
    else:
        return display
    return f'<a class="autorefs autorefs-internal" href="{anchor}">{display}</a>'


# Docstring-section tables (mkdocstrings' "Attributes:" rendering) always carry
# a Type column; the flag-enum enumerator docs have no types, leaving it empty.
# Drop the column from any table where every row's Type cell is blank.
_SECTION_TABLE_RE = re.compile(
    r"<table>\s*<thead>.*?</thead>\s*<tbody>.*?</tbody>\s*</table>", re.DOTALL)
_SECTION_ROW_RE = re.compile(
    r"(<tr[^>]*>)\s*(<td[^>]*>.*?</td>)\s*(<td[^>]*>.*?</td>)\s*(<td[^>]*>.*?</td>)\s*(</tr>)",
    re.DOTALL)


def _strip_empty_type_columns(html_out: str) -> str:
    def fix(m: re.Match) -> str:
        tbl = m.group(0)
        if "<th>Type</th>" not in tbl:
            return tbl
        rows = _SECTION_ROW_RE.findall(tbl)
        if not rows or any(re.sub(r"<[^>]+>", "", r[2]).strip() for r in rows):
            return tbl                     # no rows, or a typed row exists
        tbl = tbl.replace("<th>Type</th>", "", 1)
        return _SECTION_ROW_RE.sub(lambda r: r.group(1) + r.group(2) + r.group(4)
                                   + r.group(5), tbl)
    return _SECTION_TABLE_RE.sub(fix, html_out)


def _coerce_vectors(html_out: str, base: str) -> str:
    def sub(m):
        elem = _vector_elements().get(m.group(1))
        return f"list[{_elem_html(elem, base)}]" if elem is not None else m.group(0)
    return _VEC_SPAN_RE.sub(sub, _VEC_LINK_RE.sub(sub, html_out))


def _class_redirect(name: str) -> tuple[str, str, str] | None:
    """(page, module, anchor class) for a versioned class that renders nowhere
    under its own name: a concrete entity-family class (M2RootLegionPlus behind
    M2.root) redirects to its family base's page, a deduplicated record sibling
    to its family's representative."""
    split = split_range_suffix(name)
    if not split:
        return None
    stem = split[0]
    for fmt in formats():
        if stem in fmt.elem_links:
            page, module = fmt.elem_links[stem]
            return page, module, stem
        for sp in fmt.struct_pages:
            if sp.dedup_marker and name in sp.classes():
                rep = sp.anchor_class(name)
                if rep != name:
                    return sp.page, sp.module, rep
    return None


# An autoref mkdocstrings could not resolve ends up as a bare title-span.
_UNRESOLVED_SPAN_RE = re.compile(r'<span title="[^"]*">([A-Za-z0-9_]+)</span>')


def _retarget_class_refs(output: str, base: str) -> str:
    """Turn unresolved class references into links to the family's rendered
    documentation, displayed generically: nanobind annotates properties with
    CONCRETE versioned classes (M2.root -> M2RootLegionPlus), which since the
    generic-page restructure render nowhere under their own name."""
    def sub(m):
        target = _class_redirect(m.group(1))
        if target is None:
            return m.group(0)
        page, module, cls = target
        href = f"{base}{_page_url(page)}#{module}.{cls}"
        return (f'<a class="autorefs autorefs-internal" href="{href}">'
                f"{_generic_name(m.group(1))}</a>")
    return _UNRESOLVED_SPAN_RE.sub(sub, output)


# --- badge rendering ----------------------------------------------------------
def _pill(major: int, text: str, base: str) -> str:
    key, _short, name = EXPANSIONS[major]
    rel = _ICON_REL.get(key)
    if rel:
        # With an icon the emblem conveys the expansion, so show only the version
        # number when the text carries one (range badge "BfA 8.1" -> "8.1"); with no
        # number (legend "Vanilla", or a whole-expansion range "Cata") show the label.
        # A "< 8.1" (removed-before) label keeps its comparator.
        ver = re.search(r"<\s*\d[\d.]*|\d[\d.]*", text)
        label = ver.group(0) if ver else text
        inner = (f'<img class="exp-icon" src="{base}{rel}" alt="{html.escape(name)}">'
                 f'<span class="exp-label">{html.escape(label)}</span>')
    else:
        inner = html.escape(text)
    return f'<span class="exp-pill exp-{key}" title="{html.escape(name)}">{inner}</span>'


def _version_label(major: int, minor: int, patch: int) -> str:
    _key, short, _name = EXPANSIONS[major]
    return f"{short} {major}.{minor}" if (minor or patch) else short


def _range_html(since: tuple | None, until: tuple | None, base: str) -> str:
    """A coloured availability range. A field present in every supported version
    (no since/until) gets NO badge — only version-restricted fields are tagged.
    since = introduced (open-ended, arrow); until = removed at that version
    (exclusive). The badge reads INCLUSIVE on both ends — it shows the last
    version that still HAS the field: an era-marker removal (X.0.0, the named
    era constants) renders the PREVIOUS expansion (until WotLK -> "… TBC"),
    while a mid-expansion removal keeps its expansion with a "< 8.1" pill so
    the exact boundary stays visible. A range that collapses to one expansion
    renders a single pill."""
    if since is None and until is None:
        return ""
    if until is None:                                    # introduced, present onward
        start = _pill(since[0], _version_label(*since[:3]), base)
        return f'<span class="wmo-range">{start}<span class="wmo-bar wmo-bar--open"></span></span>'
    major, minor, patch = until[:3]
    title = f"removed in {_version_label(major, minor, patch)}"
    if minor == 0 and patch == 0:        # era marker: last present = previous expansion
        end_major, end_label = major - 1, EXPANSIONS[major - 1][1]
    else:                                # mid-expansion removal: present before it
        num = f"{major}.{minor}" if minor else f"{major}.{minor}.{patch}"
        end_major, end_label = major, f"< {num}"
    start_tuple = since if since else (1, 0, 0)
    start_label = _version_label(*start_tuple[:3]) if since else "Vanilla"
    if start_tuple[0] == end_major and start_label == end_label:
        pill = _pill(end_major, end_label, base)
        return f'<span class="wmo-range" title="{title}">{pill}</span>'
    start = _pill(start_tuple[0], start_label, base)
    removed = _pill(end_major, end_label, base)
    return (f'<span class="wmo-range" title="{title}">{start}'
            f'<span class="wmo-bar"></span>{removed}</span>')


def _cc_html(cc: str, fmt: Format) -> str:
    """The chunk FourCC badge, linking to its exact section on the format's
    wowdev.wiki page (anchor from the vendored map; unmapped -> page top)."""
    if not re.fullmatch(r"[A-Z0-9]{4}", cc or ""):
        return ""
    anchor = fmt.anchors().get(cc)
    href = f"https://wowdev.wiki/{fmt.wowdev_page}"
    if anchor:
        href += "#" + urllib.parse.quote(anchor, safe="()_-")
    return (f'<a class="wmo-cc" href="{href}" target="_blank" rel="noopener"'
            f' title="{cc} on wowdev.wiki">{cc}</a>')


def _legend(base: str) -> str:
    pills = "".join(_pill(major, short, base) for major, (key, short, name) in EXPANSIONS.items())
    return ('<div class="wmo-legend"><span class="wmo-legend-label">Expansions:</span>'
            f"{pills}</div>")


# --- HTML augmentation --------------------------------------------------------
_HEADING_RE = re.compile(
    r'(?P<open><h(?P<lvl>[1-6])\s+id="(?P<hid>[^"]+)"[^>]*class="doc doc-heading"[^>]*>)'
    r'(?P<inner>.*?)(?P<close></h(?P=lvl)>)', re.DOTALL)


def _base_prefix(page) -> str:
    stem = (page.url or "").strip("/")
    return "../" * (stem.count("/") + 1) if stem else ""


# griffe drops for_version: nanobind's stubgen emits it as all-@overload with no
# primary implementation, so griffe registers no member and mkdocstrings renders
# nothing. It is the key method of every family base, so inject a proper method entry
# (mkdocstrings' own markup) after each base class's docstring.
def _forver_block(hid: str, cls: str, example: str) -> str:
    generic = html.escape(cls + _VERSION_PLACEHOLDER)
    return (
        '<div class="doc doc-object doc-function">'
        f'<h4 id="{hid}.for_version" class="doc doc-heading">'
        '<code class="doc-symbol doc-symbol-heading doc-symbol-method"></code> '
        '<span class="doc doc-object-name doc-function-name">for_version</span> '
        '<span class="doc doc-labels"><small class="doc doc-label doc-label-staticmethod">'
        '<code>staticmethod</code></small></span>'
        f'<a href="#{hid}.for_version" class="headerlink" title="Permanent link">&para;</a></h4>'
        '<div class="language-python doc-signature highlight"><pre><code>'
        f'for_version(expansion: Expansion) -&gt; {generic}</code></pre></div>'
        '<div class="doc doc-contents">'
        f'<p>Construct the concrete <code>{cls}</code> for a client version — the abstract '
        f'<code>{cls}</code> is never instantiated directly. The return type narrows per '
        'expansion (a typed overload per <code>Expansion</code> member), so '
        f'<code>for_version(Expansion.{example})</code> returns a <code>{cls}{example}</code>; a '
        f'runtime <code>Expansion</code> value yields the <code>Any{cls}</code> union.</p>'
        '</div></div>')


def _inject_for_version(html_out: str, entries: tuple[tuple[str, str, str], ...]) -> str:
    for hid, cls, example in entries:
        pat = re.compile(
            r'(<h3 id="' + re.escape(hid) + r'" class="doc doc-heading">'
            r'.*?<div class="doc doc-contents[^"]*">\s*<p>.*?</p>)', re.DOTALL)
        html_out = pat.sub(
            lambda m, hid=hid, cls=cls, example=example:
                m.group(1) + _forver_block(hid, cls, example),
            html_out, count=1)
    return html_out


def _genericize(html_out: str, fmt: Format) -> str:
    """Show version-suffixed class names generically (display only; ids/anchors
    keep the real name). The heading name span and the signature code block are
    present at content time; cross-reference links are still autorefs
    placeholders here, so those are rewritten in on_post_page."""
    generic = rf"\1\2{_VERSION_PLACEHOLDER}\3"
    res = fmt.res()
    for key in ("classname", "sig"):
        html_out = res[key].sub(generic, html_out)
    return html_out


def _augment_fields(html_out: str, fmt: Format, base: str, src: str) -> str:
    # Any per-version class may render on this page: the representative for live
    # fields, an earlier class for a removed chunk (rendered from its last version).
    # Field info is keyed by name (identical across versions), so only the side and
    # field name matter, not the class suffix.
    for side in fmt.sides:
        if _side_page(fmt, side) != src:
            continue
        fields = side.fields()
        hid_re = re.compile(
            re.escape(side.module) + r"\.(?:" + "|".join(side.badge_classes) + r")"
            + _SUFFIX_ALT + r"\.(\w+)$")

        def repl(m, fields=fields, hid_re=hid_re):
            mm = hid_re.match(m["hid"])
            if not mm:
                return m.group(0)
            f = fields.get(mm.group(1))
            if not f or f["container"]:
                return m.group(0)
            tags = _cc_html(f["cc"], fmt) + _range_html(f["since"], f["until"], base)
            if not tags:
                return m.group(0)
            return f'{m["open"]}{m["inner"]}<span class="wmo-tags">{tags}</span>{m["close"]}'

        html_out = _HEADING_RE.sub(repl, html_out)
    # Show the representative concrete classes generically (WMOGroupWotlk ->
    # WMOGroup⟨version⟩); then complete the family bases with for_version.
    html_out = _genericize(html_out, fmt)
    return _inject_for_version(html_out, fmt.forver.get(src, ()))


def _rewrite_toc(items, toc_re: re.Pattern) -> None:
    """Rename the representative classes in the right-hand TOC too, so it matches
    the generic heading names."""
    for item in items:
        if item.title:
            item.title = toc_re.sub(rf"\1{_VERSION_PLACEHOLDER}", item.title)
        _rewrite_toc(item.children, toc_re)


def _chunk_owners(sp: StructPage) -> dict[str, list[tuple[str, str]]]:
    """wire-struct name -> [(entity field name, FourCC), …] for structs documented
    on this struct page."""
    owners: dict[str, list[tuple[str, str]]] = {}
    if sp.owner_side is None:
        return owners
    names = sp.classes()
    for f in sp.owner_side.fields().values():
        if not f["container"] and f["elem"] in names:
            owners.setdefault(f["elem"], []).append((f["name"], f["cc"]))
    return owners


def _augment_chunks(html_out: str, fmt: Format, sp: StructPage, base: str) -> str:
    owners = _chunk_owners(sp)
    struct_names = sp.classes()
    side = sp.owner_side

    def repl(m):
        mm = re.match(re.escape(sp.module) + r"\.(\w+)$", m["hid"])
        if not mm:
            return m.group(0)
        name = mm.group(1)
        parts = []
        if name in owners and side:              # a wire struct -> its entity field(s)
            for fname, cc in owners[name]:
                anchor = (f"{base}{_page_url(_side_page(fmt, side))}"
                          f"#{side.module}.{side.repr_class}.{fname}")
                parts.append(f'<a class="wmo-owner" href="{anchor}"'
                             f' title="Used by the {fname} field of the '
                             f'{fmt.key.upper()} {side.key} entity">'
                             f"[{html.escape(fname)}]</a>")
                parts.append(_cc_html(cc, fmt))
        elif name in sp.enum_chunk:              # a flag/enum -> the struct that carries it
            cc, struct = sp.enum_chunk[name]
            if struct in struct_names:           # same-page link to the wire struct
                anchor = f"#{sp.module}.{struct}"
                parts.append(f'<a class="wmo-owner" href="{anchor}"'
                             f' title="Flag bits of {struct} ({cc})">'
                             f"[{html.escape(struct)}]</a>")
            parts.append(_cc_html(cc, fmt))      # badge -> that chunk's wowdev section
        if not parts:
            return m.group(0)
        return f'{m["open"]}{m["inner"]}<span class="wmo-tags">{"".join(parts)}</span>{m["close"]}'

    return _HEADING_RE.sub(repl, html_out)


# --- wire integer widths ------------------------------------------------------
# nanobind coerces every fixed-width C++ integer (std::uint32_t, std::int16_t, …)
# to a bare Python `int`, so the on-disk field size vanishes from the stubs. The
# width lives only in the C++ sources; parse it back and re-annotate the rendered
# signatures as Annotated[int, uint32] so the wire size stays documented.
# The optional close is CONDITIONAL on what opened: a std::vector chain closes
# with its `>`s, a std::array with `, N>`, a bare scalar with nothing — so a
# struct member typed `FBlock<std::uint16_t> foo` is NOT mis-read as a uint16
# field (its `>` is a template close the regex must not consume). `inarr`
# handles a std::array nested in a vector (vector<array<uint8, 4>> — per-vertex
# bone quads, the TXAC combos). The annotation group forbids `]]` so it cannot
# bleed across a PRECEDING excluded member's brackets (which would drag its
# `mark::exclude` onto this member and wrongly drop it).
_STRUCT_MEMBER_RE = re.compile(
    r"(?:\[\[(?P<ann>(?:(?!\]\]).)*)\]\]\s*)?"          # optional annotation (no `]]` inside)
    r"(?:std::)?"                                       # std:: of array/vector/int
    r"(?:(?P<arr>array<\s*)|(?P<vec>(?:(?:std::)?vector<\s*)+))?(?:std::)?"
    r"(?P<inarr>array<\s*(?:std::)?)?"                  # std::array nested in a vector
    r"(?P<sign>u?)int(?P<bits>8|16|32|64)_t"
    # array extents may be constant EXPRESSIONS (17 * 17), not just literals
    r"(?(inarr)\s*,\s*\d[\d\s*+]*>)?"                   # inner array close
    r"(?(vec)\s*>+|(?(arr)\s*,\s*\d[\d\s*+]*>|))"
    r"\s+(?P<field>\w+)\s*[={;]", re.DOTALL)
_INT_SPAN = '<span title="int">int</span>'


def _width_label(unsigned: bool, bits: int) -> str:
    """The wmo-int marker span rendered inside Annotated[int, …]."""
    label = f"{'u' if unsigned else ''}int{bits}"
    title = f"{'unsigned ' if unsigned else 'signed '}{bits}-bit integer on the wire"
    return f'<span class="wmo-int" title="{title}">{label}</span>'


def _int_width(elem: str) -> tuple[bool, int] | None:
    """(unsigned, bits) for a fixed-width C++ integer type name (uint16_t), else
    None (StringBlock, ChunkBlob, a wire struct, float, …)."""
    m = re.fullmatch(r"(u?)int(8|16|32|64)_t", elem or "")
    return (m.group(1) == "u", int(m.group(2))) if m else None


_STRUCT_DECL_RE = re.compile(
    r"(?:struct|class|enum\s+class)\s+(?:\[\[.*?\]\]\s*)?"
    r"(?:WOWLIB_EMPTY_BASES\s+)?(?P<name>[A-Za-z_]\w*)(?:<[^>]*>)?"
    r"\s*(?::[^{;]*)?\{", re.DOTALL)
_BLANK_STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"', re.DOTALL)
_BLANK_BLOCK_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
_BLANK_LINE_RE = re.compile(r"//[^\n]*")


def _blank_noncode(txt: str) -> str:
    """A same-length copy of ``txt`` with string literals and comments blanked
    to spaces, so brace matching sees only code braces — never a `{`/`}` (or a
    stray apostrophe) inside a doc string or a comment. Strings go first so a
    `//` inside one is not mistaken for a comment; block before line."""
    txt = _BLANK_STRING_RE.sub(lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', txt)
    txt = _BLANK_BLOCK_RE.sub(lambda m: " " * len(m.group(0)), txt)
    return _BLANK_LINE_RE.sub(lambda m: " " * len(m.group(0)), txt)


def _match_brace(txt: str, open_pos: int) -> int:
    """The index of the `}` matching the `{` at ``open_pos`` (or end-of-text)."""
    depth = 0
    for i in range(open_pos, len(txt)):
        if txt[i] == "{":
            depth += 1
        elif txt[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(txt)


def _struct_int_fields(headers: tuple[Path, ...]) -> dict[str, dict[str, tuple[bool, int, bool]]]:
    """{C++ struct name: {field: (unsigned, bits, vectorish)}} for the wire
    structs declared in ``headers`` — vectorish marks std::vector/array members
    (possibly nested), whose rendered annotation is a coerced ``list[…int…]``
    rather than an int autoref span. Each struct's region is BRACE-MATCHED and
    its nested struct bodies blanked, so a struct that contains one (SMOFog
    holds Fog; LightExtension holds Gradient) keeps the members that follow the
    nested type instead of losing them to it."""
    out: dict[str, dict[str, tuple[bool, int, bool]]] = {}
    for path in headers:
        txt = path.read_text(encoding="utf-8")
        braces = _blank_noncode(txt)      # for brace matching / decl detection
        structs = [(m.group("name"), m.start(), m.end() - 1,
                    _match_brace(braces, m.end() - 1))
                   for m in _STRUCT_DECL_RE.finditer(braces)]
        for name, dstart, open_pos, close_pos in structs:
            region = list(txt[open_pos + 1: close_pos])
            for _n2, ds2, _op2, cl2 in structs:          # blank nested struct spans
                if ds2 > open_pos and cl2 <= close_pos and ds2 != dstart:
                    for k in range(ds2, min(cl2 + 1, close_pos)):
                        region[k - (open_pos + 1)] = " "
            fields: dict[str, tuple[bool, int, bool]] = {}
            for mm in _STRUCT_MEMBER_RE.finditer("".join(region)):
                if mm.group("ann") and "mark::exclude" in mm.group("ann"):
                    continue
                # std::array members count as vectorish too: since welder's
                # opaque Array* wrappers they coerce to plain-text list[int]
                # exactly like opaque vectors (no int autoref span left).
                fields[mm.group("field")] = (
                    mm.group("sign") == "u", int(mm.group("bits")),
                    mm.group("vec") is not None or mm.group("inarr") is not None
                    or mm.group("arr") is not None)
            if fields:
                out.setdefault(name, {}).update(fields)
    return out


def _cpp_struct(cls: str, alias: dict[str, str], known: set[str] = frozenset()) -> str:
    """The C++ wire-struct name for a rendered class: strip the range suffix,
    undo any explicit weld aliasing, then resolve a welded TEMPLATE class whose
    value argument welder mangled into the name (M2TrackC3Vector -> M2Track,
    FBlockUInt16 -> FBlock, M2SplineKeyFloat -> M2SplineKey) down to the base
    struct — the longest ``known`` struct name it starts with, since all value
    variants of one template share its int fields."""
    split = split_range_suffix(cls)
    if split:
        cls = split[0]
    cls = alias.get(cls, cls)
    if cls in known:
        return cls
    prefixes = [k for k in known if cls.startswith(k) and cls != k]
    return max(prefixes, key=len) if prefixes else cls


def _annotate_int_widths(html_out: str, sp: StructPage) -> str:
    ints = sp.int_fields()
    # Constrain the class group to the classes this page actually renders: the
    # module is a namespace PREFIX of sibling struct pages (wowlib.formats.m2 vs
    # …m2.skin), so an unconstrained `\w+` would match a sibling's heading
    # (…m2.skin.M2Batch as cls=skin) and its `.*?</hN>` would swallow the region
    # up to the next signature — starving the real headings between.
    cls_alt = "|".join(re.escape(c) for c in sorted(sp.classes(), key=len, reverse=True))
    if not cls_alt:
        return html_out
    # The heading body uses (?:(?!</hN>).)*? so it CANNOT cross its own close —
    # otherwise a heading not followed by a signature div (an enum/flag class,
    # e.g. LightExtension) would backtrack `.*?` to a later heading and swallow
    # the real attribute headings in between.
    pat = re.compile(
        r'(?P<pre><h(?P<lvl>[1-6]) id="' + re.escape(sp.module) +
        r'\.(?P<cls>' + cls_alt + r')\.(?P<field>\w+)"[^>]*>'
        r'(?:(?!</h(?P=lvl)>).)*?</h(?P=lvl)>\s*'
        r'<div class="[^"]*doc-signature[^"]*">)(?P<sig>.*?)(?P<end></div>)', re.DOTALL)

    known = set(ints)

    def repl(m):
        info = ints.get(_cpp_struct(m["cls"], sp.struct_alias, known), {}).get(m["field"])
        if info is None:
            # A welded template's VALUE member (values/keys/…) is typed on the
            # bare parameter `T`, so its width lives only in the class-name value
            # suffix (M2TrackUInt16 -> uint16), not the C++ decl.
            info = _value_member_width(m["cls"], m["field"], sp.struct_alias, known)
        if info is None:
            return m.group(0)
        sig = _apply_int_width(m["sig"], *info)
        return m["pre"] + sig + m["end"] if sig is not None else m.group(0)

    return pat.sub(repl, html_out)


# Welded value-type suffixes that are raw integers (M2TrackUInt16, FBlockUInt16,
# …); the float/fixed16/vector value types are structs and stay unannotated.
_VALUE_SUFFIX_WIDTH = {"UInt8": (True, 8), "UInt16": (True, 16), "UInt32": (True, 32),
                       "Int8": (False, 8), "Int16": (False, 16), "Int32": (False, 32)}
# The template members typed on the parameter T across the track/fblock family.
_VALUE_MEMBER_NAMES = frozenset({"values", "keys", "value", "in_tan", "out_tan"})


def _value_member_width(cls: str, field: str, alias: dict[str, str],
                        known: set[str]) -> tuple[bool, int, bool] | None:
    """(unsigned, bits, vectorish=True) for a welded-template value member whose
    width is carried by the class-name value suffix (M2TrackUInt16.values ->
    uint16), else None. Vectorish is always True — the value member is a
    vector<T> (or vector<vector<T>>); scalar-T value types (M2SplineKey.value)
    never reach here because those variants are non-integer."""
    if field not in _VALUE_MEMBER_NAMES:
        return None
    base = _cpp_struct(cls, alias, known)
    split = split_range_suffix(cls)
    core = alias.get(split[0] if split else cls, cls) if not split else split[0]
    core = alias.get(core, core)
    if not core.startswith(base) or core == base:
        return None
    w = _VALUE_SUFFIX_WIDTH.get(core[len(base):])
    return (w[0], w[1], True) if w else None


def _apply_int_width(sig: str, unsigned: bool, bits: int, vectorish: bool) -> str | None:
    """Rewrite the int in a rendered signature as Annotated[int, uintN], or None
    when there is nothing to rewrite. A scalar/array carries the `int` autoref
    span; a coerced opaque vector is plain-text list[int] / list[list[int]]
    (the nesting depth read from the signature itself)."""
    label = _width_label(unsigned, bits)
    if _INT_SPAN in sig:                   # scalar (or std::array rendered as list[int-span])
        return sig.replace(_INT_SPAN, f'Annotated[{_INT_SPAN}, {label}]', 1)
    if vectorish:
        lm = re.search(r"((?:list\[)+)int((?:\])+)", sig)
        if lm:
            return (sig[:lm.start()] + lm.group(1) + f"Annotated[int, {label}]"
                    + lm.group(2) + sig[lm.end():])
    return None


def _annotate_entity_int_widths(html_out: str, fmt: Format, src: str) -> str:
    """Fields page: annotate the entity members whose (element) type is a
    fixed-width integer. Scalars (mver) render as `<span title="int">int</span>`;
    opaque int vectors have already been coerced to plain `list[int]` by
    _coerce_vectors (this must run after it), so both forms are handled."""
    for side in fmt.sides:
        if _side_page(fmt, side) != src:
            continue
        fields = side.fields()
        pat = re.compile(
            r'(?P<pre><h(?P<lvl>[1-6]) id="' + re.escape(side.module) +
            r'\.(?:' + "|".join(side.width_classes) + r')' + _SUFFIX_ALT +
            r'\.(?P<field>\w+)"[^>]*>'
            r'(?:(?!</h(?P=lvl)>).)*?</h(?P=lvl)>\s*'
            r'<div class="[^"]*doc-signature[^"]*">)(?P<sig>.*?)(?P<end></div>)',
            re.DOTALL)

        def repl(m, fields=fields):
            f = fields.get(m["field"])
            w = f.get("int_width") if f else None
            if not w:
                return m.group(0)
            # vectorish=True: an opaque int vector (or vector-of-array) has been
            # coerced to plain list[int] / list[list[int]]; a scalar keeps its
            # int autoref span — _apply_int_width picks the right one.
            sig = _apply_int_width(m["sig"], w[0], w[1], vectorish=True)
            return m["pre"] + sig + m["end"] if sig is not None else m.group(0)

        html_out = pat.sub(repl, html_out)
    return html_out


# --- deduplicated record-page markdown generation -----------------------------
def _era_words(suffix: str) -> str:
    """A range suffix in words: "Wotlk" -> "WotLK only", "VanillaToTbc" ->
    "Vanilla – TBC", "CataPlus" -> "Cata+"."""
    m = _RANGE_RE.match(suffix)
    if not m:
        return suffix
    first = EXPANSIONS[EXP_SUFFIXES.index(m.group(1)) + 1][1]
    if m.group(3):
        return f"{first}+"
    if m.group(2):
        return f"{first} – {EXPANSIONS[EXP_SUFFIXES.index(m.group(2)) + 1][1]}"
    return f"{first} only"


# page -> {heading id: (since, until)} for the merged record listings — filled
# by _records_markdown (markdown phase), consumed by _augment_record_badges
# (content phase of the same page/build).
_RECORD_BADGES: dict[str, dict[str, tuple]] = {}
# page -> {heading id} of value-template value members, whose rendered value type
# is rewritten to the generic ⟨value⟩ (post-page phase, after vector coercion).
_RECORD_VALUE_MEMBERS: dict[str, set[str]] = {}


def _suffix_span(suffix: str) -> tuple[int, int | None]:
    """(first major, last major — None when open-ended) a range suffix covers."""
    m = _RANGE_RE.match(suffix)
    first = EXP_SUFFIXES.index(m.group(1)) + 1
    if m.group(3):
        return first, None
    last = EXP_SUFFIXES.index(m.group(2)) + 1 if m.group(2) else first
    return first, last


def _directive(sp: StructPage, target: str, level: int, extra: str = "") -> str:
    return (f"::: {sp.module}.{target}\n    options:\n"
            "      show_root_heading: true\n"
            "      show_root_toc_entry: true\n"
            f"      heading_level: {level}" + extra)


def _vt_base(name: str, bases) -> str | None:
    """The value-template base ``name`` is an instance of (M2TrackC4Quaternion ->
    M2Track), or None. Bases do not prefix one another, so a plain prefix test
    with a non-empty value remainder suffices."""
    for b in bases:
        if name.startswith(b) and len(name) > len(b):
            return b
    return None


def _records_families(sp: StructPage):
    """The page's render families, in stub order. Each is
    ("plain", stem, [(rank, name)]) — a normal version family — or
    ("vt", base, value, [(rank, name)]) — a value template collapsed to its
    CANONICAL value variant (the first value seen), whose value member(s) the
    caller shows generically. All other value variants are dropped (identical
    but for the value member)."""
    bases = set(sp.value_templates)
    order: list = []
    plain: dict[str, list] = {}
    vt: dict[str, dict[str, list]] = {}
    for name, _block in _stub_class_blocks(sp.stub):
        if sp.names_filter and not sp.names_filter(name):
            continue
        base = _vt_base(name, bases)
        if base:
            split = split_range_suffix(name)
            value = (split[0] if split else name)[len(base):]
            rank = range_rank(split[1]) if split else -1
            if base not in vt:
                order.append(("vt", base))
            vt.setdefault(base, {}).setdefault(value, []).append((rank, name))
        else:
            split = split_range_suffix(name)
            stem, rank = (split[0], range_rank(split[1])) if split else (name, -1)
            if stem not in plain:
                order.append(("plain", stem))
            plain.setdefault(stem, []).append((rank, name))
    out = []
    for kind, key in order:
        if kind == "plain":
            out.append(("plain", key, sorted(plain[key])))
        else:
            canon = next(iter(vt[key]))            # first value variant, stub order
            out.append(("vt", key, canon, sorted(vt[key][canon])))
    return out


def _emit_merged_members(sp, blocks, badges, out, stem, fam, value_members):
    """Emit the per-member era-merged directives for one family (the shared body
    of a plain multi-era family and a value-template family). Each member renders
    once per distinct layout, badged when not family-wide; a member in
    ``value_members`` is recorded so its value type renders as the generic
    ⟨value⟩."""
    classes = [cls for _r, cls in fam]
    props_of = {cls: _class_props(blocks[cls]) for cls in classes}
    order = list(props_of[classes[-1]].keys())
    for cls in reversed(classes[:-1]):             # splice older-only members in
        prev = None
        for name in props_of[cls]:
            if name in order:
                prev = name
                continue
            order.insert(order.index(prev) + 1 if prev is not None else 0, name)
            prev = name
    vgen = _RECORD_VALUE_MEMBERS.setdefault(sp.page, set())
    for name in order:
        variants: list[dict] = []
        prev_had = False
        for cls in classes:
            got = props_of[cls].get(name)
            if got is None:
                prev_had = False
                continue
            if prev_had and variants[-1]["ann"] == got[0]:
                variants[-1]["classes"].append(cls)
            else:
                variants.append({"ann": got[0], "classes": [cls]})
            prev_had = True
        family_wide = (len(variants) == 1
                       and len(variants[0]["classes"]) == len(classes))
        for var in variants:
            cls = var["classes"][-1]
            out.append(_directive(sp, f"{cls}.{name}", 4))
            if name in value_members:
                vgen.add(f"{sp.module}.{cls}.{name}")
            if family_wide:
                continue
            # The era span from the class RANGE suffix (a value-template family is
            # keyed on its base, so cls[len(stem):] would include the value part).
            first_split = split_range_suffix(var["classes"][0])
            last_split = split_range_suffix(var["classes"][-1])
            if not first_split or not last_split:
                continue                     # a value axis with no version axis
            first, _last = _suffix_span(first_split[1])
            _first, last = _suffix_span(last_split[1])
            # A span reaching the latest supported expansion is open-ended for
            # display (the db grids name the last era exactly — "TheWarWithin",
            # not a Plus range — and an until of LATEST+1 has no EXPANSIONS row).
            until = (None if last is None or last >= LATEST_MAJOR
                     else (last + 1, 0, 0))
            badges[f"{sp.module}.{cls}.{name}"] = ((first, 0, 0), until)


def _class_docstring(block: str) -> str:
    """The class-level docstring of a stub class block (the first triple-quoted
    string before any member), whitespace-collapsed."""
    m = re.match(r'class [^\n]*:\s*"""(.*?)"""', block, re.S)
    return re.sub(r"\s+", " ", m.group(1)).strip() if m else ""


def _records_markdown(sp: StructPage) -> str:
    """The deduplicated record listing behind a struct page's dedup marker,
    mirroring how wowdev documents a versioned struct: ONE merged member walk
    per family. A normal family's heading + docstring render from the latest
    representative (shown generically); a value-template family (M2Track,
    FBlock, …) collapses its ~10 welded value variants into ONE listing under a
    hand-written `Base⟨value⟩` heading, its value member(s) shown generically.
    Below either, every (member, layout) renders exactly once — from the newest
    class that declares it — badged when not family-wide. Ranges derive from
    which era classes declare the member, so they cannot drift."""
    blocks = dict(_stub_class_blocks(sp.stub))
    badges = _RECORD_BADGES.setdefault(sp.page, {})
    out: list[str] = []
    for fam_desc in _records_families(sp):
        if fam_desc[0] == "vt":
            _, base, value, fam = fam_desc
            doc = _class_docstring(blocks[fam[-1][1]])
            out.append(f"### {base}{_VALUE_PLACEHOLDER} {{#{sp.module}.{base}}}")
            if doc:
                out.append(doc)
            out.append(f"*A template over the value type; the value member(s) "
                       f"below hold `{_VALUE_PLACEHOLDER}`. Referenced as e.g. "
                       f"`{base}[C4Quaternion]`.*")
            _emit_merged_members(sp, blocks, badges, out, base, fam,
                                 sp.value_templates[base])
            continue
        _, stem, fam = fam_desc
        versioned = [(r, n) for r, n in fam if r >= 0]
        bases = [n for r, n in fam if r < 0]
        if not versioned:                    # a plain unversioned struct/enum
            out.append(_directive(sp, fam[-1][1], 3))
            continue
        if len(versioned) == 1 and not bases:  # exists in one range, no base
            out.append(_directive(sp, versioned[0][1], 3))
            out.append(f"*One layout across its whole range "
                       f"({_era_words(versioned[0][1][len(stem):])}).*")
            continue
        # Multi-era, and/or a welded family base. Anchor + heading: a welded base
        # (WMOBatch) is a version-agnostic name with no suffix to genericize, so
        # it gets a hand-written `Base⟨version⟩` heading + its own docstring; a
        # baseless family (M2 records) renders its latest version generically.
        # Either way only the VERSIONED classes are walked for members (an empty
        # base would add spurious badges).
        anchor = _family_anchor(fam)
        if bases:
            doc = _class_docstring(blocks.get(anchor, ""))
            out.append(f"### {anchor}{_VERSION_PLACEHOLDER} {{#{sp.module}.{anchor}}}")
            if doc:
                out.append(doc)
        else:
            out.append(_directive(sp, anchor, 3, "\n      members: false"))
        _emit_merged_members(sp, blocks, badges, out, stem, versioned, set())
    return "\n\n".join(out)


def _augment_record_badges(html_out: str, sp: StructPage, base: str) -> str:
    """Inject the expansion-range badges _records_markdown computed onto the
    merged member headings of a deduplicated record page."""
    badges = _RECORD_BADGES.get(sp.page)
    if not badges:
        return html_out

    def repl(m):
        r = badges.get(m["hid"])
        # several StructPages share one records page; skip already-tagged rows
        if not r or 'class="wmo-tags"' in m["inner"]:
            return m.group(0)
        tags = _range_html(r[0], r[1], base)
        if not tags:
            return m.group(0)
        return f'{m["open"]}{m["inner"]}<span class="wmo-tags">{tags}</span>{m["close"]}'

    return _HEADING_RE.sub(repl, html_out)


# --- value-template genericization + generic-type references -------------------
# The value member's rendered type is the canonical value: a coerced list chain
# (list[list[<a>C3Vector</a>]]) or a scalar autoref (<a>C3Vector</a>) — the ONE
# link/type-name span in the signature. Replace it with the ⟨value⟩ placeholder.
_SIG_LEAF_RE = re.compile(
    r'<a\b[^>]*class="autorefs[^"]*"[^>]*>[^<]+</a>'
    r'|<span title="[^"]*">[A-Za-z_][\w.]*</span>')


def _genericize_value_members(output: str, page: str) -> str:
    """Rewrite the value member(s) of every collapsed value-template family on
    ``page`` to hold the generic ⟨value⟩ (run after vector coercion, so the leaf
    is the sole link/type-span inside the rendered signature)."""
    ids = _RECORD_VALUE_MEMBERS.get(page)
    if not ids:
        return output
    pat = re.compile(
        r'(<h[1-6] id="(?P<hid>[\w.]+)"[^>]*>(?:(?!</h[1-6]>).)*?</h[1-6]>\s*'
        r'<div class="[^"]*doc-signature[^"]*">)(?P<sig>.*?)(?P<end></div>)', re.DOTALL)

    def repl(m):
        if m["hid"] not in ids:
            return m.group(0)
        sig = _SIG_LEAF_RE.sub(
            lambda _m: f'<span class="doc-value-t">{_VALUE_PLACEHOLDER}</span>',
            m["sig"], count=1)
        return m.group(1) + sig + m["end"]

    return pat.sub(repl, output)


def _vt_registry() -> dict[str, tuple[str, str, set[str]]]:
    """base -> (page, module, value members) for every value-template family
    declared across the loaded formats' struct pages."""
    reg: dict[str, tuple[str, str, set[str]]] = {}
    for fmt in formats():
        for sp in fmt.struct_pages:
            for base, members in sp.value_templates.items():
                reg.setdefault(base, (sp.page, sp.module, members))
    return reg


def _vt_value_alias() -> dict[str, str]:
    """The value-suffix aliases merged across formats (value templates are one
    format's concern today, but the reference rewriter is format-agnostic)."""
    merged: dict[str, str] = {}
    for fmt in formats():
        merged.update(fmt.value_alias)
    return merged


def _vt_split(name: str, reg: dict) -> tuple[str, str] | None:
    """(base, value suffix) if ``name`` is a value-template instance, else None.
    The range suffix, if any (M2TrackC3VectorWotlkPlus), is dropped — every era
    of one value renders as the same Base[Value]."""
    base = _vt_base(name, reg)
    if not base:
        return None
    split = split_range_suffix(name)
    core = split[0] if split else name
    return (base, core[len(base):]) if len(core) > len(base) else None


def _type_token_html(token: str, value_alias: dict, reg: dict, base_url: str) -> str:
    """Render a value token as linked HTML: a nested value template recurses
    (M2SplineKeyC3Vector -> M2SplineKey[C3Vector]), a documented struct links,
    a Python builtin (float/int) stays plain."""
    vt = _vt_split(token, reg)
    if vt:
        return _vt_ref_html(vt[0], vt[1], value_alias, reg, base_url)
    if token in ("int", "float", "bool", "str"):
        return token
    return _elem_html(token, base_url)          # links known structs, else plain text


def _vt_ref_html(base: str, value: str, value_alias: dict, reg: dict,
                 base_url: str) -> str:
    """`Base[Value]` HTML: Base links to its collapsed family doc, Value to its
    own (recursively)."""
    page, module, _members = reg[base]
    href = f"{base_url}{_page_url(page)}#{module}.{base}"
    base_html = f'<a class="autorefs autorefs-internal" href="{href}">{base}</a>'
    value_html = _type_token_html(value_alias.get(value, value), value_alias, reg, base_url)
    return f"{base_html}[{value_html}]"


def _render_value_template_refs(output: str, base_url: str) -> str:
    """Rewrite every reference to a concrete value-template class (a resolved
    autoref link or an unresolved title span) into the generic `Base[Value]`
    form, so the ~25 welded permutations read as the four templates they are."""
    reg = _vt_registry()
    if not reg:
        return output
    value_alias = _vt_value_alias()
    alt = "|".join(re.escape(b) for b in sorted(reg, key=len, reverse=True))
    ref_re = re.compile(
        r'<a\b[^>]*>(?P<a>(?:' + alt + r')[A-Za-z0-9]*)</a>'
        r'|<span title="[^"]*">(?P<s>(?:' + alt + r')[A-Za-z0-9]*)</span>')

    def repl(m):
        name = m["a"] or m["s"]
        vt = _vt_split(name, reg)
        if not vt:
            return m.group(0)
        return _vt_ref_html(vt[0], vt[1], value_alias, reg, base_url)

    return ref_re.sub(repl, output)


# --- fields-page markdown generation ------------------------------------------
def _version_class_fields(side: Side) -> dict[str, dict[str, tuple[str, str]]]:
    """{version class name: {field: (type, docstring)}} for a side's per-version
    classes, introspected from the stubs."""
    txt = _stub_text(side.stub)
    out: dict[str, dict[str, tuple[str, str]]] = {}
    for block in re.split(r"(?=^class )", txt, flags=re.M):
        m = re.match(rf"class ({side.class_prefix}{_SUFFIX_ALT})[:(]", block)
        if not m:
            continue
        fields: dict[str, tuple[str, str]] = {}
        for fm in re.finditer(
                r'def ([a-z]\w+)\(self\)\s*->\s*([^:]+):\s*(?:"""(.*?)""")?', block, re.S):
            fields.setdefault(fm.group(1),
                              (fm.group(2).strip(), (fm.group(3) or "").strip()))
        out[m.group(1)] = fields
    return out


def _field_source_class(side: Side) -> dict[str, str]:
    """field name -> the version class the fields page renders it from: the
    representative when it still declares the field, else the latest earlier class
    that does (a field removed before the representative, e.g. WMO root MOTX at
    8.3 or M2's pre-WotLK embedded skin profiles)."""
    classes = _version_class_fields(side)
    have: dict[str, list[tuple[int, str]]] = {}
    for cls, flds in classes.items():
        suf = cls[len(side.class_prefix):]
        rank = range_rank(suf)
        if rank < 0:
            continue
        for name in flds:
            have.setdefault(name, []).append((rank, cls))
    out: dict[str, str] = {}
    for name, ranked in have.items():
        out[name] = (side.repr_class if any(c == side.repr_class for _, c in ranked)
                     else max(ranked)[1])
    return out


def _category_slug(cat: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", cat.lower()).strip("-")


def _fields_markdown(fmt: Format, side: Side) -> str:
    """The category-grouped field sections for one side: a `### Category` heading
    per non-empty category (in the side's category order), each followed by
    mkdocstrings directives that render just that category's members. Members come
    from the representative class, except removed fields, which render from their
    last version class — so added and removed fields sit together in their
    section."""
    fields = side.fields()
    source = _field_source_class(side)

    ordered = (f for f in fields.values() if not f["container"])
    if side.sort_key is not None:
        ordered = sorted(ordered, key=side.sort_key)

    buckets: dict[str, list[tuple[str, str]]] = {c: [] for c in side.category_order}
    unmapped: list[str] = []
    for f in ordered:
        cls = source.get(f["name"])
        if not cls:                              # no stub declares it (nothing to render)
            continue
        cat = side.categorize(f)
        if cat is None:
            unmapped.append(f["cc"] or f["name"])
            cat = side.category_order[0]
        buckets[cat].append((f["name"], cls))
    if unmapped:
        print(f"wowlib-docs: WARNING uncategorized {fmt.key} {side.key} fields (add "
              f"to the config's category map): {', '.join(sorted(set(unmapped)))}",
              flush=True)

    out: list[str] = []
    for cat in side.category_order:
        members = buckets[cat]
        if not members:
            continue
        out.append(f"### {cat} {{#{side.anchor}-{_category_slug(cat)}}}")
        if side.category_blurbs.get(cat):
            out.append(side.category_blurbs[cat])
        # One directive per member, addressed as Class.attr (not a class with a
        # members filter) — an attribute carries no "Bases:"/class-docstring to leak
        # into the section. A removed field names an earlier class than the rest.
        for name, cls in members:
            out.append(f"::: {side.module}.{cls}.{name}\n"
                       "    options:\n"
                       "      show_root_heading: true\n"
                       "      show_root_toc_entry: true\n"
                       "      heading_level: 4")
    return "\n\n".join(out)


# --- mkdocs hooks -------------------------------------------------------------
def on_page_markdown(markdown, page, config, files, **kwargs):
    src = page.file.src_uri
    for fmt in formats():
        if src in _side_pages(fmt) and fmt.legend_marker in markdown:
            markdown = markdown.replace(fmt.legend_marker, _legend(_base_prefix(page)))
        for side in fmt.sides:
            if src == _side_page(fmt, side) and side.marker in markdown:
                markdown = markdown.replace(side.marker, _fields_markdown(fmt, side))
        for sp in fmt.struct_pages:
            if sp.dedup_marker and src == sp.page and sp.dedup_marker in markdown:
                markdown = markdown.replace(sp.dedup_marker, _records_markdown(sp))
    return markdown


def on_page_content(html_out, page, config, files, **kwargs):
    src = page.file.src_uri
    base = _base_prefix(page)
    for fmt in formats():
        try:
            if src in _side_pages(fmt):
                _rewrite_toc(page.toc, fmt.res()["toc"])
                html_out = _augment_fields(html_out, fmt, base, src)
            elif src in fmt.generic_pages:
                _rewrite_toc(page.toc, fmt.res()["toc"])
                html_out = _genericize(html_out, fmt)
                html_out = _inject_for_version(html_out, fmt.forver.get(src, ()))
            for sp in fmt.struct_pages:
                if sp.page == src and (sp.owner_side or sp.enum_chunk):
                    html_out = _augment_chunks(html_out, fmt, sp, base)
                if sp.page == src and sp.dedup_marker:
                    html_out = _augment_record_badges(html_out, sp, base)
        except (OSError, KeyError, re.error):
            continue
    return html_out


def on_post_page(output, page, config, **kwargs):
    """Post-render cleanups on the fully-rendered page (after autorefs resolves).

    - All Python API pages: coerce opaque-vector type annotations to
      list[element].
    - Struct/record pages: re-annotate wire integer fields with their C++ width
      (int -> Annotated[int, uint32]).
    - Fields + generic pages: rewrite cross-reference link text (e.g. the
      `body`/`header` property types) to the generic …⟨version⟩ name — these are
      autorefs placeholders during on_page_content, real <a> tags only now.
      Hrefs untouched.

    The int-type span is an unresolved autoref during on_page_content and only
    becomes `<span title="int">int</span>` here, so the width pass must run now.
    """
    src = page.file.src_uri
    base = _base_prefix(page)
    try:
        if src.startswith("python/"):
            output = _coerce_vectors(output, base)
            # vt refs BEFORE _retarget_class_refs and the xref genericization:
            # a dropped value variant (M2TrackSplineC3Vector…) would otherwise be
            # retargeted to a now-missing anchor or genericized to ⟨version⟩.
            output = _render_value_template_refs(output, base)
            output = _retarget_class_refs(output, base)
            output = _genericize_value_members(output, src)
            output = _strip_empty_type_columns(output)
    except re.error:
        return output
    for fmt in formats():
        try:
            for sp in fmt.struct_pages:
                if sp.page == src and sp.headers:
                    output = _annotate_int_widths(output, sp)
            if src in _side_pages(fmt):
                output = _annotate_entity_int_widths(output, fmt, src)
            if src in _side_pages(fmt) or src in fmt.generic_pages:
                output = fmt.res()["xref"].sub(rf"\1\2{_VERSION_PLACEHOLDER}\3", output)
        except (OSError, re.error):
            continue
    return output
