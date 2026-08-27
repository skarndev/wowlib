"""WDL config for the format-reference engine (format_reference_impl.py).

Data + small functions only. The WDL is a single chunked entity (one side);
its per-tile chunks (MARE/MAOE/MAHO) are `repeating` members, which parse
like any other chunk member. The wire structs (tile heightmaps, LOD
placements, sky scenes) render on the chunks page.
"""

from __future__ import annotations

import re

import format_reference_impl as fr

WDL_SRC = fr.REPO_ROOT / "src/wowlib/formats/wdl"
BUILDS_HPP = fr.REPO_ROOT / "src/wowlib/core/client_builds.hpp"
BOUNDARIES_HPP = WDL_SRC / "boundaries.hpp"
WDL_HPP = WDL_SRC / "wdl.hpp"

_CONSTS_CACHE: dict | None = None


def _consts() -> dict[str, tuple[int, int, int]]:
    global _CONSTS_CACHE
    if _CONSTS_CACHE is None:
        _CONSTS_CACHE = fr.parse_version_constants(BUILDS_HPP, BOUNDARIES_HPP)
    return _CONSTS_CACHE


# --- fields-page category taxonomy -------------------------------------------
CATEGORY_ORDER = ("Header", "Object placements", "Sky scenes", "Tile table")
FOURCC_CATEGORY = {
    "MVER": "Header",
    "MWMO": "Object placements", "MWID": "Object placements",
    "MODF": "Object placements",
    "MLDD": "Object placements", "MLDX": "Object placements",
    "MLDF": "Object placements", "MLDL": "Object placements",
    "MLDB": "Object placements", "MLMD": "Object placements",
    "MLMX": "Object placements", "MLMB": "Object placements",
    "MSSN": "Sky scenes", "MSSC": "Sky scenes", "MSSO": "Sky scenes",
    "MSSF": "Sky scenes", "MSLD": "Sky scenes", "MSLI": "Sky scenes",
    "MAOF": "Tile table", "MARE": "Tile table", "MAOC": "Tile table",
    "MAOE": "Tile table", "MAHO": "Tile table",
}
CATEGORY_BLURB = {
    "Header": "The file version.",
    "Object placements": "The low-resolution model silhouettes: WMO names and "
                         "placements up to WoD, the MLDD/MLMD FileDataID "
                         "placements since Legion.",
    "Sky scenes": "Distant scripted scenery (Shadowlands+): sky scenes, their "
                  "conditions, placed objects and schedules.",
    "Tile table": "The 64 x 64 tile offset table and the per-tile heightmap, "
                  "hole-mask and ocean-mask chunks it addresses.",
}


# --- entity member parsing ----------------------------------------------------
_FIELDS_CACHE: dict | None = None


def _entity_fields() -> dict[str, dict]:
    """WDL members: the detail:: trait slots (version-gated) plus the struct's
    own members — the slice from `namespace detail` to end-of-file covers
    both."""
    global _FIELDS_CACHE
    if _FIELDS_CACHE is None:
        text = fr.slice_text(WDL_HPP.read_text(encoding="utf-8"), "namespace detail", None)
        fields = {f["name"]: f for f in fr.parse_members(text, consts=_consts())}
        fields.pop("Version", None)   # the struct-head parse artifact
        _FIELDS_CACHE = fields
    return _FIELDS_CACHE


_ORDER_CACHE: list[str] | None = None


def _chunk_order() -> list[str]:
    global _ORDER_CACHE
    if _ORDER_CACHE is None:
        m = re.search(r"ChunkOrder\s*=\s*\{(.*?)\}",
                      WDL_HPP.read_text(encoding="utf-8"), re.DOTALL)
        _ORDER_CACHE = re.findall(r'fourCc\("([^"]+)"\)', m.group(1)) if m else []
    return _ORDER_CACHE


def _rank(f) -> int:
    order = _chunk_order()
    return order.index(f["cc"]) if f["cc"] in order else len(order)


# --- the side -------------------------------------------------------------------
WDL_SIDE = fr.Side(
    key="wdl", kind="chunked",
    marker="<!-- wdl-fields -->",
    page="python/wdl/entity.md",
    module="wowlib.formats.wdl",
    badge_classes=("WDL",), width_classes=("WDL",),
    class_prefix="WDL",
    # Latest version = largest field set; the pre-Legion-only MAOC and the
    # pre-Legion object trio render from the last class that has them via the
    # engine's removed-field resolution.
    repr_class="WDLTheWarWithin",
    stub="wowlib/formats/wdl/__init__.pyi",
    parse_fields=_entity_fields,
    categorize=lambda f: FOURCC_CATEGORY.get(f["cc"]),
    category_order=CATEGORY_ORDER, category_blurbs=CATEGORY_BLURB,
    anchor="wdl",
    sort_key=_rank,
)

# --- the chunk-struct page ------------------------------------------------------
CHUNKS = fr.StructPage(
    page="python/wdl/chunks.md",
    module="wowlib.formats.wdl.chunks",
    stub="wowlib/formats/wdl/chunks.pyi",
    headers=(WDL_SRC / "chunks/tiles.hpp", WDL_SRC / "chunks/objects.hpp",
             WDL_SRC / "chunks/skyscene.hpp"),
    owner_side=WDL_SIDE,
    dedup_marker="<!-- wdl-chunks -->",
)

FORMAT = fr.Format(
    key="wdl",
    fields_page="python/wdl/entity.md",
    legend_marker="<!-- wdl-legend -->",
    sides=(WDL_SIDE,),
    wowdev_page="WDL/v18",
    anchors_file="wdl_wowdev_anchors.json",
    name_re=r"WDL[A-Za-z]*?",
    generic_pages=frozenset({"python/wdl/entity.md", "python/wdl/chunks.md"}),
    forver={
        "python/wdl/entity.md": (
            ("wowlib.formats.wdl.WDL", "WDL", "Wotlk"),
        ),
    },
    struct_pages=(CHUNKS,),
    elem_links={
        "WDL": ("python/wdl/entity.md", "wowlib.formats.wdl"),
    },
)
