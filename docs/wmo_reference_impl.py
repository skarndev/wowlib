"""WMO reference augmentation logic (reloadable impl behind wmo_reference.py).

The mkdocs hook is a thin shim (``wmo_reference.py``) that reloads THIS module on
every build, so edits here take effect under ``mkdocs serve`` without a restart
(mkdocs otherwise caches an imported hook for the process's life). Keep the event
handlers (``on_page_markdown``/``on_page_content``) here.

--- augment the WMO API pages with version + chunk metadata.

The WMO layout is version-parametric (`WMO<V>` per expansion). The entity headers
(`root/root.hpp`, `group/group.hpp`) carry, per chunk member, its `=chunk()` FourCC,
its `=since()`/`=until()` client versions (the single source of truth for when a
field exists — mirroring wowdev.wiki) and which wire struct it is built from. This
hook reads those and decorates the mkdocstrings-rendered pages:

  * **Fields page** (`python/wmo/fields.md`): mkdocstrings renders the entity
    classes (base + a representative concrete version) natively — docstrings,
    methods, attributes. This hook injects, onto each attribute heading, a FourCC
    badge (linking wowdev.wiki) and an expansion-range badge (only when the field
    is version-restricted; all-version fields stay clean).
  * **Chunk pages** (`root-chunks`/`group-chunks`): onto each wire-struct heading it
    injects which entity field(s) use it (`[materials]`, linking back to the fields
    page) plus that field's FourCC.

Rendering rides on mkdocstrings' own heading markup, so the badges inherit the
exact class style. The expansion badges use icons from
`content/assets/expansions/<key>.{svg,png,webp}` when present, else colored text
pills. Fail-safe: parse errors leave the page untouched.
"""

from __future__ import annotations

import html
import json
import re
import urllib.parse
from pathlib import Path

DOCS_DIR = Path(__file__).resolve().parent
REPO_ROOT = DOCS_DIR.parent
ROOT_HPP = REPO_ROOT / "src/wowlib/formats/wmo/root/root.hpp"
GROUP_HPP = REPO_ROOT / "src/wowlib/formats/wmo/group/group.hpp"
STUBS = REPO_ROOT / "build/bindings/bindings/python/stubs"

TARGET_PAGE = "python/wmo/fields.md"
CHUNK_PAGES = {"python/wmo/root-chunks.md": "root", "python/wmo/group-chunks.md": "group"}
MARKER_LEGEND = "<!-- wmo-legend -->"
# The concrete representative class each side's attributes/structs are documented on.
REPR_CLASS = {"root": "WMORootWotlk", "group": "WMOGroupBodyWotlk"}
# Its expansion suffix — stripped from the *displayed* class name on the fields page
# so the per-version layout reads generically (WMOGroupWotlk -> WMOGroup⟨version⟩),
# signalling the fields apply to every version. Ids/anchors keep the real name.
REPR_SUFFIX = "Wotlk"
_VERSION_PLACEHOLDER = "⟨version⟩"
_CLASSNAME_RE = re.compile(
    r'(<span class="doc doc-object-name doc-class-name">)(WMO[A-Za-z]+?)'
    + REPR_SUFFIX + r'(</span>)')
# The same name where mkdocstrings echoes it: the signature code block (pygments
# name span) and cross-reference link text. Ids/hrefs (…="…Wotlk") are untouched,
# so links still resolve; only the shown text goes generic. Hand-written prose uses
# <code>…</code> and is deliberately left alone.
_SIG_NAME_RE = re.compile(r'(<span class="n[cf]">)(WMO[A-Za-z]+?)' + REPR_SUFFIX + r'(</span>)')
_XREF_NAME_RE = re.compile(r'(<a\b[^>]*>)(WMO[A-Za-z]+?)' + REPR_SUFFIX + r'(</a>)')
_TOC_TITLE_RE = re.compile(r'\b(WMO[A-Za-z]+?)' + REPR_SUFFIX + r'\b')

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

# FourCC -> wowdev.wiki WMO-page anchor (vendored; refresh with build.py).
try:
    WOWDEV_ANCHORS: dict[str, str] = json.loads(
        (DOCS_DIR / "wmo_wowdev_anchors.json").read_text(encoding="utf-8"))
except OSError:
    WOWDEV_ANCHORS = {}

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


# The opaque Vector wrappers (VectorUnsignedInt, VectorC3Vector, VectorSMOMaterial,
# VectorSMOBatchWotlk, …) mirror the list interface with the same guarantees, but
# their generated names read poorly in the docs. Coerce every Vector type annotation
# to list[element]. The element (its true Python name, incl. versioned ones like
# WMOBatchWotlk) comes from the stub's append() signature; a representative-version
# suffix is shown generically (WMOBatchWotlk -> WMOBatch⟨version⟩) to match the page.
def _vector_elements() -> dict[str, str]:
    p = STUBS / "wowlib/__init__.pyi"
    out: dict[str, str] = {}
    try:
        txt = p.read_text(encoding="utf-8")
    except OSError:
        return out
    for block in re.split(r"(?=^class )", txt, flags=re.M):
        m = re.match(r"class (Vector[A-Za-z0-9_]+)[:(]", block)
        if not m:
            continue
        em = (re.search(r"def append\(self, arg:\s*([A-Za-z0-9_.]+)", block)
              or re.search(r"def __getitem__\(self, arg: int[^)]*\)\s*->\s*([A-Za-z0-9_.]+)", block))
        if not em:
            continue
        elem = em.group(1).split(".")[-1]
        if elem != REPR_SUFFIX and elem.endswith(REPR_SUFFIX):
            elem = elem[: -len(REPR_SUFFIX)] + _VERSION_PLACEHOLDER
        out[m.group(1)] = elem       # bare element name; list[…] built at rewrite time
    return out


_VECTOR_ELEMS = _vector_elements()
# In the rendered HTML a Vector type annotation is either a resolved cross-ref link
# (documented vectors) or an unresolved autoref title-span (the rest). Rewrite both.
_VEC_LINK_RE = re.compile(r'<a\b[^>]*>(Vector[A-Za-z0-9_]+)</a>')
_VEC_SPAN_RE = re.compile(r'<span title="[^"]*\b(Vector[A-Za-z0-9_]+)">\1</span>')

_CHUNK_SIDE_CACHE: dict[str, str] | None = None


def _chunk_struct_side(name: str) -> str | None:
    """Which chunks page (root/group) documents a wire struct, or None. Lazy so it
    runs after _chunk_names is defined; reset per build when the module reloads."""
    global _CHUNK_SIDE_CACHE
    if _CHUNK_SIDE_CACHE is None:
        _CHUNK_SIDE_CACHE = {}
        for side in ("root", "group"):
            for n in _chunk_names(side):
                _CHUNK_SIDE_CACHE.setdefault(n, side)
    return _CHUNK_SIDE_CACHE.get(name)


_COMMON_CACHE: set[str] | None = None


def _common_names() -> set[str]:
    """Struct names documented on the Common structures page (from the stub)."""
    global _COMMON_CACHE
    if _COMMON_CACHE is None:
        try:
            txt = (STUBS / "wowlib/formats/common.pyi").read_text(encoding="utf-8")
            _COMMON_CACHE = set(re.findall(r"^class ([A-Za-z0-9_]+)", txt, re.M))
        except OSError:
            _COMMON_CACHE = set()
    return _COMMON_CACHE


def _elem_html(elem: str, base: str) -> str:
    """The element inside list[…], linked to its documentation when it is a known
    struct: a WMO wire struct on its chunks page (SMOPoly, SMOMaterial,
    WMOBatch⟨version⟩ → its base), or a shared primitive on the Common page
    (C3Vector, CArgb, …)."""
    lookup = elem.replace(_VERSION_PLACEHOLDER, "")
    side = _chunk_struct_side(lookup)
    if side:
        anchor = f"{base}python/wmo/{side}-chunks/#wowlib.formats.wmo.{side}.chunks.{lookup}"
    elif lookup in _common_names():
        anchor = f"{base}python/common/#wowlib.formats.common.{lookup}"
    else:
        return elem
    return f'<a class="autorefs autorefs-internal" href="{anchor}">{elem}</a>'


def _coerce_vectors(html_out: str, base: str) -> str:
    def sub(m):
        elem = _VECTOR_ELEMS.get(m.group(1))
        return f"list[{_elem_html(elem, base)}]" if elem is not None else m.group(0)
    return _VEC_SPAN_RE.sub(sub, _VEC_LINK_RE.sub(sub, html_out))


# --- badge rendering ---------------------------------------------------------
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


def _cc_html(cc: str) -> str:
    """The chunk FourCC badge, linking to its exact section on wowdev.wiki's WMO
    page (anchor from the vendored map; unmapped -> page top)."""
    if not re.fullmatch(r"[A-Z0-9]{4}", cc or ""):
        return ""
    anchor = WOWDEV_ANCHORS.get(cc)
    href = "https://wowdev.wiki/WMO"
    if anchor:
        href += "#" + urllib.parse.quote(anchor, safe="()_-")
    return (f'<a class="wmo-cc" href="{href}" target="_blank" rel="noopener"'
            f' title="{cc} on wowdev.wiki">{cc}</a>')


def _legend(base: str) -> str:
    pills = "".join(_pill(major, short, base) for major, (key, short, name) in EXPANSIONS.items())
    return ('<div class="wmo-legend"><span class="wmo-legend-label">Expansions:</span>'
            f"{pills}</div>")


# --- source parsing ----------------------------------------------------------
_CV = r"ClientVersion\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\d+\s*\}"
_BLOCK = re.compile(r"\[\[(?P<attrs>.*?)\]\]\s*(?P<decl>[^;]*?);", re.DOTALL)


def _slice(text: str, start: str, end: str | None) -> str:
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i) if end else len(text)
    return text[i: j if j > 0 else len(text)]


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


def _parse_members(text: str) -> list[dict]:
    out = []
    for m in _BLOCK.finditer(text):
        attrs, decl = m.group("attrs"), m.group("decl")
        if "welder::mark::exclude" in attrs:
            continue
        cc_m = re.search(r'chunk\("([^"]+)"\)', attrs)
        is_header = "formats::header" in attrs           # the MOGP group-header prelude
        toks = re.findall(r"[A-Za-z_]\w*", re.sub(r"\{[^}]*\}", "", decl.split("=")[0]))
        if not toks:
            continue
        since = re.search(r"since\(\s*" + _CV, attrs)
        until = re.search(r"until\(\s*" + _CV, attrs)
        out.append({
            "name": toks[-1],
            "cc": cc_m.group(1) if cc_m else ("MOGP" if is_header else ""),
            "since": tuple(int(x) for x in since.groups()) if since else None,
            "until": tuple(int(x) for x in until.groups()) if until else None,
            "elem": _elem_name(decl, name=toks[-1]),
            "container": "formats::container" in attrs,
        })
    return out


_FIELDS_CACHE: dict | None = None


def _entity_fields() -> tuple[dict, dict]:
    """(root_fields, group_fields): field name -> parsed info. Group merges the
    MOGP body members with the WMOGroup wrapper's (mver)."""
    global _FIELDS_CACHE
    if _FIELDS_CACHE is None:
        root = {f["name"]: f for f in
                _parse_members(_slice(ROOT_HPP.read_text(encoding="utf-8"), "]] WMORoot :", None))}
        gtxt = GROUP_HPP.read_text(encoding="utf-8")
        # WMOGroupBody's version-gated chunks live in detail:: trait bases declared
        # ahead of the struct, so slice from the detail namespace (traits) through
        # WMOGroupBody's own members, up to the WMOGroup wrapper.
        group = {f["name"]: f for f in
                 _parse_members(_slice(gtxt, "namespace detail", "]] WMOGroup :"))}
        for f in _parse_members(_slice(gtxt, "]] WMOGroup :", None)):
            group.setdefault(f["name"], f)
        _FIELDS_CACHE = (root, group)
    return _FIELDS_CACHE


def _chunk_names(side: str) -> set[str]:
    p = STUBS / f"wowlib/formats/wmo/{side}/chunks.pyi"
    try:
        return set(re.findall(r"^class ([A-Za-z0-9_]+)", p.read_text(encoding="utf-8"), re.M))
    except OSError:
        return set()


def _chunk_owners(side: str) -> dict[str, list[tuple[str, str]]]:
    """wire-struct name -> [(entity field name, FourCC), …] for structs documented
    on the <side>-chunks page."""
    fields = _entity_fields()[0 if side == "root" else 1]
    names = _chunk_names(side)
    owners: dict[str, list[tuple[str, str]]] = {}
    for f in fields.values():
        if not f["container"] and f["elem"] in names:
            owners.setdefault(f["elem"], []).append((f["name"], f["cc"]))
    return owners


# --- HTML augmentation -------------------------------------------------------
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
_FORVER_RE = re.compile(
    r'(<h3 id="(wowlib\.formats\.wmo\.(?:root|group)\.(?:WMOGroupBody|WMOGroup|WMORoot))"'
    r' class="doc doc-heading">.*?<div class="doc doc-contents[^"]*">\s*<p>.*?</p>)',
    re.DOTALL)


def _forver_block(hid: str, cls: str) -> str:
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
        f'<code>for_version(Expansion.Wotlk)</code> returns a <code>{cls}Wotlk</code>; a '
        f'runtime <code>Expansion</code> value yields the <code>Any{cls}</code> union.</p>'
        '</div></div>')


def _inject_for_version(html_out: str) -> str:
    def repl(m):
        hid = m.group(2)
        return m.group(1) + _forver_block(hid, hid.rsplit(".", 1)[-1])
    return _FORVER_RE.sub(repl, html_out)


def _augment_fields(html_out: str, base: str) -> str:
    root, group = _entity_fields()
    classes = "|".join(REPR_CLASS.values())

    def repl(m):
        mm = re.match(rf"wowlib\.formats\.wmo\.(root|group)\.(?:{classes})\.(\w+)$", m["hid"])
        if not mm:
            return m.group(0)
        f = (root if mm.group(1) == "root" else group).get(mm.group(2))
        if not f or f["container"]:
            return m.group(0)
        tags = _cc_html(f["cc"]) + _range_html(f["since"], f["until"], base)
        if not tags:
            return m.group(0)
        return f'{m["open"]}{m["inner"]}<span class="wmo-tags">{tags}</span>{m["close"]}'

    html_out = _HEADING_RE.sub(repl, html_out)
    # Show the representative concrete class generically (WMOGroupWotlk ->
    # WMOGroup⟨version⟩) — display only; ids/anchors keep the real name. The heading
    # name span and the signature code block are present now; cross-reference links
    # are still autorefs placeholders here, so those are rewritten in on_post_page.
    generic = rf"\1\2{_VERSION_PLACEHOLDER}\3"
    for rx in (_CLASSNAME_RE, _SIG_NAME_RE):
        html_out = rx.sub(generic, html_out)
    return _inject_for_version(html_out)


def _rewrite_toc(items) -> None:
    """Rename the representative classes in the right-hand TOC too, so it matches
    the generic heading names."""
    for item in items:
        if item.title:
            item.title = _TOC_TITLE_RE.sub(rf"\1{_VERSION_PLACEHOLDER}", item.title)
        _rewrite_toc(item.children)


def _augment_chunks(html_out: str, side: str, base: str) -> str:
    owners = _chunk_owners(side)
    cls = REPR_CLASS[side]

    def repl(m):
        mm = re.match(rf"wowlib\.formats\.wmo\.{side}\.chunks\.(\w+)$", m["hid"])
        if not mm or mm.group(1) not in owners:
            return m.group(0)
        parts = []
        for fname, cc in owners[mm.group(1)]:
            anchor = f"{base}python/wmo/fields/#wowlib.formats.wmo.{side}.{cls}.{fname}"
            parts.append(f'<a class="wmo-owner" href="{anchor}"'
                         f' title="Used by the {fname} field of the WMO {side} entity">'
                         f"[{html.escape(fname)}]</a>")
            parts.append(_cc_html(cc))
        return f'{m["open"]}{m["inner"]}<span class="wmo-tags">{"".join(parts)}</span>{m["close"]}'

    return _HEADING_RE.sub(repl, html_out)


# --- mkdocs hooks ------------------------------------------------------------
def on_page_markdown(markdown, page, config, files, **kwargs):
    if page.file.src_uri == TARGET_PAGE and MARKER_LEGEND in markdown:
        return markdown.replace(MARKER_LEGEND, _legend(_base_prefix(page)))
    return markdown


def on_page_content(html_out, page, config, files, **kwargs):
    try:
        src = page.file.src_uri
        base = _base_prefix(page)
        if src == TARGET_PAGE:
            _rewrite_toc(page.toc)
            return _augment_fields(html_out, base)
        if src in CHUNK_PAGES:
            return _augment_chunks(html_out, CHUNK_PAGES[src], base)
    except (OSError, KeyError, re.error):
        return html_out
    return html_out


def on_post_page(output, page, config, **kwargs):
    """Post-render cleanups on the fully-rendered page (after autorefs resolves).

    - All Python API pages: coerce scalar opaque-vector type annotations to
      list[int]/list[float].
    - Fields page: rewrite cross-reference link text (e.g. the `body`/`header`
      property types) to the generic WMO…⟨version⟩ name — these are autorefs
      placeholders during on_page_content, real <a> tags only now. Hrefs untouched.
    """
    try:
        src = page.file.src_uri
        if src.startswith("python/"):
            output = _coerce_vectors(output, _base_prefix(page))
        if src == TARGET_PAGE:
            output = _XREF_NAME_RE.sub(rf"\1\2{_VERSION_PLACEHOLDER}\3", output)
    except re.error:
        return output
    return output
