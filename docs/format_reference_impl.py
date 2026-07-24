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
# (an element class documented by two formats links to the first).
FORMAT_MODULES = ("wmo_reference_config", "m2_reference_config")

# Expansion suffixes (welded class names), stripped from the *displayed* class
# name so the per-version layout reads generically (WMOGroupBodyTheWarWithin ->
# WMOGroupBody⟨version⟩); ids/anchors keep the real name so links resolve.
EXP_SUFFIXES = ("Vanilla", "Tbc", "Wotlk", "Cata", "Mop", "Wod", "Legion", "Bfa",
                "Shadowlands", "Dragonflight", "TheWarWithin")
_SUFFIX_ALT = "(?:" + "|".join(EXP_SUFFIXES) + ")"
_VERSION_PLACEHOLDER = "⟨version⟩"


def _generic_name(name: str) -> str:
    """Strip a trailing expansion suffix, replacing it with ⟨version⟩."""
    for suf in EXP_SUFFIXES:
        if name != suf and name.endswith(suf):
            return name[: -len(suf)] + _VERSION_PLACEHOLDER
    return name


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
    _classes: set[str] | None = dc_field(default=None, repr=False)
    _ints: dict | None = dc_field(default=None, repr=False)

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


def parse_version_constants(path: Path) -> dict[str, tuple[int, int, int]]:
    """Named ``inline constexpr ClientVersion`` boundaries (e.g. M2's
    ``m2_per_sequence_timelines``) -> (major, minor, patch), so annotations may
    reference them instead of literals."""
    out: dict[str, tuple[int, int, int]] = {}
    for m in re.finditer(
            r"inline\s+constexpr\s+ClientVersion\s+(\w+)\s*"
            r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
            path.read_text(encoding="utf-8")):
        out[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
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


def _version_ref(attrs: str, kind: str,
                 consts: dict[str, tuple] | None) -> tuple | None:
    """The =since()/=until() version in an annotation block: a ClientVersion
    literal, or a named boundary constant resolved through ``consts``."""
    m = re.search(kind + r"\(\s*" + _CV, attrs)
    if m:
        return tuple(int(x) for x in m.groups())
    m = re.search(kind + r"\(\s*([A-Za-z_]\w*)\s*\)", attrs)
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
        out.append({
            "name": toks[-1],
            "cc": cc_m.group(1) if cc_m else (header_cc if is_header else ""),
            "since": _version_ref(attrs, "since", consts),
            "until": _version_ref(attrs, "until", consts),
            "elem": _elem_name(decl, name=toks[-1]),
            "container": "formats::container" in attrs,
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
        m = re.match(r"class (Vector[A-Za-z0-9_]+)[:(]", block)
        if not m:
            continue
        em = (re.search(r"def append\(self, arg:\s*([A-Za-z0-9_.]+)", block)
              or re.search(r"def __getitem__\(self, arg: int[^)]*\)\s*->\s*([A-Za-z0-9_.]+)", block))
        if not em:
            continue
        elem = _generic_name(em.group(1).split(".")[-1])
        out[m.group(1)] = elem       # bare element name; list[…] built at rewrite time
    _VECTOR_ELEMS_CACHE = out
    return out


# In the rendered HTML a Vector type annotation is either a resolved cross-ref link
# (documented vectors) or an unresolved autoref title-span (the rest). Rewrite both.
_VEC_LINK_RE = re.compile(r'<a\b[^>]*>(Vector[A-Za-z0-9_]+)</a>')
_VEC_SPAN_RE = re.compile(r'<span title="[^"]*\b(Vector[A-Za-z0-9_]+)">\1</span>')

# Element class name -> (page src_uri, module) for linking inside list[…]:
# explicit per-format elem_links first, then every struct page's stub classes
# (first format/page wins). Lazy so it runs after the configs load; reset per
# build when the module reloads.
_ELEM_PAGE_CACHE: dict[str, tuple[str, str]] | None = None


def _elem_pages() -> dict[str, tuple[str, str]]:
    global _ELEM_PAGE_CACHE
    if _ELEM_PAGE_CACHE is None:
        _ELEM_PAGE_CACHE = {}
        for fmt in formats():
            for name, target in fmt.elem_links.items():
                _ELEM_PAGE_CACHE.setdefault(name, target)
            for sp in fmt.struct_pages:
                for n in sp.classes():
                    _ELEM_PAGE_CACHE.setdefault(n, (sp.page, sp.module))
    return _ELEM_PAGE_CACHE


_COMMON_CACHE: set[str] | None = None


def _common_names() -> set[str]:
    """Struct names documented on the Common structures page (from the stub)."""
    global _COMMON_CACHE
    if _COMMON_CACHE is None:
        _COMMON_CACHE = _stub_classes("wowlib/formats/common.pyi")
    return _COMMON_CACHE


def _elem_html(elem: str, base: str) -> str:
    """The element inside list[…], linked to its documentation when it is a known
    struct: a wire struct/record on its format's struct page (SMOPoly,
    WMOBatch⟨version⟩ → its base, M2Sequence⟨version⟩ → its base), or a shared
    primitive on the Common page (C3Vector, CArgb, …)."""
    lookup = elem.replace(_VERSION_PLACEHOLDER, "")
    target = _elem_pages().get(lookup)
    if target:
        page, module = target
        anchor = f"{base}{_page_url(page)}#{module}.{lookup}"
    elif lookup in _common_names():
        anchor = f"{base}python/common/#wowlib.formats.common.{lookup}"
    else:
        return elem
    return f'<a class="autorefs autorefs-internal" href="{anchor}">{elem}</a>'


def _coerce_vectors(html_out: str, base: str) -> str:
    def sub(m):
        elem = _vector_elements().get(m.group(1))
        return f"list[{_elem_html(elem, base)}]" if elem is not None else m.group(0)
    return _VEC_SPAN_RE.sub(sub, _VEC_LINK_RE.sub(sub, html_out))


# --- badge rendering ----------------------------------------------------------
def _pill(major: int, text: str, base: str) -> str:
    key, _short, name = EXPANSIONS[major]
    rel = _ICON_REL.get(key)
    if rel:
        # With an icon the emblem conveys the expansion, so show only the version
        # number when the text carries one (range badge "BfA 8.1" -> "8.1"); with no
        # number (legend "Vanilla", or a whole-expansion range "Cata") show the label.
        ver = re.search(r"\d[\d.]*", text)
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
    since = introduced (open-ended, arrow); until = removed at that exact version
    (bounded, plain bar) — the removal point is shown as-is (mid-expansion aware),
    never `until.major - 1`, which mis-renders a mid-expansion boundary."""
    if since is None and until is None:
        return ""
    if until is None:                                    # introduced, present onward
        start = _pill(since[0], _version_label(*since[:3]), base)
        return f'<span class="wmo-range">{start}<span class="wmo-bar wmo-bar--open"></span></span>'
    # removed at `until` (exclusive): bounded range ending at the removal version
    start = (_pill(since[0], _version_label(*since[:3]), base) if since
             else _pill(1, "Vanilla", base))
    removed = _pill(until[0], _version_label(*until[:3]), base)
    title = f"removed in {_version_label(*until[:3])}"
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
                anchor = (f"{base}{_page_url(fmt.fields_page)}"
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
_STRUCT_MEMBER_RE = re.compile(
    r"(?:\[\[(?P<ann>.*?)\]\]\s*)?"                    # optional annotation
    r"(?:std::)?(?P<arr>array<\s*)?(?:std::)?"          # optional std::array<
    r"(?P<sign>u?)int(?P<bits>8|16|32|64)_t"
    r"(?:\s*,\s*\d+\s*>)?\s+(?P<field>\w+)\s*[={;]", re.DOTALL)
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


def _struct_int_fields(headers: tuple[Path, ...]) -> dict[str, dict[str, tuple[bool, int]]]:
    """{C++ struct name: {field: (unsigned, bits)}} for the wire structs declared
    in ``headers``. Struct regions run from one struct/enum opening brace to the
    next; the member regex only matches fixed-width int declarations, so
    doc-string braces in between are harmless."""
    out: dict[str, dict[str, tuple[bool, int]]] = {}
    for path in headers:
        txt = path.read_text(encoding="utf-8")
        spans = [(m.start(), m.end(), m.group("name")) for m in re.finditer(
            r"(?:struct|class|enum\s+class)\s+(?:\[\[.*?\]\]\s*)?"
            r"(?:WOWLIB_EMPTY_BASES\s+)?(?P<name>[A-Za-z_]\w*)(?:<[^>]*>)?"
            r"\s*(?::[^{;]*)?\{", txt, re.DOTALL)]
        for i, (_s, e, name) in enumerate(spans):
            region = txt[e: spans[i + 1][0] if i + 1 < len(spans) else len(txt)]
            fields: dict[str, tuple[bool, int]] = {}
            for mm in _STRUCT_MEMBER_RE.finditer(region):
                if mm.group("ann") and "mark::exclude" in mm.group("ann"):
                    continue
                fields[mm.group("field")] = (mm.group("sign") == "u",
                                             int(mm.group("bits")))
            if fields:
                out.setdefault(name, {}).update(fields)
    return out


def _cpp_struct(cls: str, alias: dict[str, str]) -> str:
    """The C++ wire-struct name for a rendered class (strip version suffix, undo
    any templated-struct weld aliasing)."""
    for suf in EXP_SUFFIXES:
        if cls != suf and cls.endswith(suf):
            cls = cls[: -len(suf)]
            break
    return alias.get(cls, cls)


def _annotate_int_widths(html_out: str, sp: StructPage) -> str:
    ints = sp.int_fields()
    pat = re.compile(
        r'(?P<pre><h(?P<lvl>[1-6]) id="' + re.escape(sp.module) +
        r'\.(?P<cls>\w+)\.(?P<field>\w+)"[^>]*>.*?</h(?P=lvl)>\s*'
        r'<div class="[^"]*doc-signature[^"]*">)(?P<sig>.*?)(?P<end></div>)', re.DOTALL)

    def repl(m):
        info = ints.get(_cpp_struct(m["cls"], sp.struct_alias), {}).get(m["field"])
        if not info or _INT_SPAN not in m["sig"]:
            return m.group(0)
        ann = f'Annotated[{_INT_SPAN}, {_width_label(*info)}]'
        return m["pre"] + m["sig"].replace(_INT_SPAN, ann, 1) + m["end"]

    return pat.sub(repl, html_out)


def _annotate_entity_int_widths(html_out: str, fmt: Format) -> str:
    """Fields page: annotate the entity members whose (element) type is a
    fixed-width integer. Scalars (mver) render as `<span title="int">int</span>`;
    opaque int vectors have already been coerced to plain `list[int]` by
    _coerce_vectors (this must run after it), so both forms are handled."""
    for side in fmt.sides:
        fields = side.fields()
        pat = re.compile(
            r'(?P<pre><h(?P<lvl>[1-6]) id="' + re.escape(side.module) +
            r'\.(?:' + "|".join(side.width_classes) + r')' + _SUFFIX_ALT +
            r'\.(?P<field>\w+)"[^>]*>'
            r'.*?</h(?P=lvl)>\s*<div class="[^"]*doc-signature[^"]*">)(?P<sig>.*?)(?P<end></div>)',
            re.DOTALL)

        def repl(m, fields=fields):
            f = fields.get(m["field"])
            if not f:
                return m.group(0)
            w = _int_width(f["elem"])
            if not w:
                return m.group(0)
            label = _width_label(*w)
            sig = m["sig"]
            if _INT_SPAN in sig:                                   # scalar int (mver)
                sig = sig.replace(_INT_SPAN, f'Annotated[{_INT_SPAN}, {label}]', 1)
            elif "list[int]" in sig:                              # coerced int vector
                sig = sig.replace("list[int]", f'list[Annotated[int, {label}]]', 1)
            else:
                return m.group(0)
            return m["pre"] + sig + m["end"]

        html_out = pat.sub(repl, html_out)
    return html_out


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
        if suf not in EXP_SUFFIXES:
            continue
        rank = EXP_SUFFIXES.index(suf)
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
        if src != fmt.fields_page:
            continue
        if fmt.legend_marker in markdown:
            markdown = markdown.replace(fmt.legend_marker, _legend(_base_prefix(page)))
        for side in fmt.sides:
            if side.marker in markdown:
                markdown = markdown.replace(side.marker, _fields_markdown(fmt, side))
    return markdown


def on_page_content(html_out, page, config, files, **kwargs):
    src = page.file.src_uri
    base = _base_prefix(page)
    for fmt in formats():
        try:
            if src == fmt.fields_page:
                _rewrite_toc(page.toc, fmt.res()["toc"])
                html_out = _augment_fields(html_out, fmt, base, src)
            elif src in fmt.generic_pages:
                _rewrite_toc(page.toc, fmt.res()["toc"])
                html_out = _genericize(html_out, fmt)
                html_out = _inject_for_version(html_out, fmt.forver.get(src, ()))
            for sp in fmt.struct_pages:
                if sp.page == src and (sp.owner_side or sp.enum_chunk):
                    html_out = _augment_chunks(html_out, fmt, sp, base)
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
    try:
        if src.startswith("python/"):
            output = _coerce_vectors(output, _base_prefix(page))
    except re.error:
        return output
    for fmt in formats():
        try:
            for sp in fmt.struct_pages:
                if sp.page == src and sp.headers:
                    output = _annotate_int_widths(output, sp)
            if src == fmt.fields_page:
                output = _annotate_entity_int_widths(output, fmt)
            if src == fmt.fields_page or src in fmt.generic_pages:
                output = fmt.res()["xref"].sub(rf"\1\2{_VERSION_PLACEHOLDER}\3", output)
        except (OSError, re.error):
            continue
    return output
