"""WDT config for the format-reference engine (format_reference_impl.py).

Data + small functions only. A WDT is the main .wdt file plus up to four
satellite files, each its own chunked entity — so the format has FIVE field
surfaces ("sides"): the main file on its page, and the four satellites sharing
one. Every side's entity header carries, per chunk member, its `=chunk()`
FourCC and `=since()`/`=until()` client versions — the single source of truth
the engine renders badges from.
"""

from __future__ import annotations

import re

import format_reference_impl as fr

WDT_SRC = fr.REPO_ROOT / "src/wowlib/formats/wdt"
BUILDS_HPP = fr.REPO_ROOT / "src/wowlib/core/client_builds.hpp"
BOUNDARIES_HPP = WDT_SRC / "boundaries.hpp"

_CONSTS_CACHE: dict | None = None


def _consts() -> dict[str, tuple[int, int, int]]:
    """since()/until() spell named build constants plus the WDT boundary
    aliases; builds parse first so aliases resolve."""
    global _CONSTS_CACHE
    if _CONSTS_CACHE is None:
        _CONSTS_CACHE = fr.parse_version_constants(BUILDS_HPP, BOUNDARIES_HPP)
    return _CONSTS_CACHE


# --- fields-page category taxonomies ------------------------------------------
# Keyed by chunk FourCC, like WMO's; pure documentation structure. A FourCC
# missing from a map is logged and dropped into the first category so the
# omission is visible, never silent.
ROOT_CATEGORY_ORDER = ("Header", "Tile table", "Global WMO")
ROOT_FOURCC_CATEGORY = {
    "MVER": "Header", "MPHD": "Header", "MANM": "Header",
    "MAIN": "Tile table", "MAID": "Tile table",
    "MWMO": "Global WMO", "MODF": "Global WMO",
}
ROOT_CATEGORY_BLURB = {
    "Header": "The file version, the map-wide header and the Shadowlands anima "
              "paths.",
    "Tile table": "Which of the 64 x 64 map tiles exist, and (since 8.1) the "
                  "FileDataIDs serving each.",
    "Global WMO": "The single global map object of a WMO-only map.",
}

_SATELLITE_CATEGORY = {
    "occ": ("Occlusion", "Per-tile low-resolution heightmaps the renderer "
                         "occludes distant geometry against."),
    "lgt": ("Lights", "The map's freely placed point and spot lights, their "
                      "textures and animations."),
    "fogs": ("Fogs", "Placed volumetric fog volumes."),
    "mpv": ("Particulate volumes", "Weather particulate volumes in repeated "
                                   "PVMI/PVPD/PVBD groups."),
}


# --- entity member parsing ----------------------------------------------------
_FIELDS_CACHE: dict[str, dict] = {}

_SIDE_HPP = {
    "root": WDT_SRC / "root/root.hpp",
    "occ": WDT_SRC / "occlusion/occlusion.hpp",
    "lgt": WDT_SRC / "lights/lights.hpp",
    "fogs": WDT_SRC / "fogs/fogs.hpp",
    "mpv": WDT_SRC / "mpv/mpv.hpp",
}


def _fields(side_key: str):
    """Entity members: the detail:: trait slots (version-gated) plus the
    struct's own members — the slice from `namespace detail` to end-of-file
    covers both."""
    def parse() -> dict[str, dict]:
        if side_key not in _FIELDS_CACHE:
            text = fr.slice_text(_SIDE_HPP[side_key].read_text(encoding="utf-8"),
                                 "namespace detail", None)
            fields = {f["name"]: f for f in fr.parse_members(text, consts=_consts())}
            fields.pop("version", None)   # the struct-head parse artifact
            _FIELDS_CACHE[side_key] = fields
        return _FIELDS_CACHE[side_key]
    return parse


_ORDER_CACHE: dict[str, list[str]] = {}


def _chunk_order(side_key: str) -> list[str]:
    """The entity's canonical chunk FourCC order (its `static constexpr
    chunk_order` table when present, else header declaration order)."""
    if side_key not in _ORDER_CACHE:
        txt = _SIDE_HPP[side_key].read_text(encoding="utf-8")
        m = re.search(r"chunk_order\s*=\s*\{(.*?)\}", txt, re.DOTALL)
        _ORDER_CACHE[side_key] = re.findall(r'four_cc\("([^"]+)"\)', m.group(1)) if m else []
    return _ORDER_CACHE[side_key]


def _rank(side_key: str):
    def key(f):
        order = _chunk_order(side_key)
        return order.index(f["cc"]) if f["cc"] in order else len(order)
    return key


def _satellite_side(side_key: str, class_name: str, module_leaf: str,
                    repr_class: str) -> fr.Side:
    category, blurb = _SATELLITE_CATEGORY[side_key]
    return fr.Side(
        key=side_key, kind="chunked",
        marker=f"<!-- wdt-{side_key}-fields -->",
        page="python/wdt/satellites.md",
        module=f"wowlib.formats.wdt.{module_leaf}",
        badge_classes=(class_name,), width_classes=(class_name,),
        class_prefix=class_name,
        repr_class=repr_class,
        stub=f"wowlib/formats/wdt/{module_leaf}/__init__.pyi",
        parse_fields=_fields(side_key),
        categorize=lambda f: category,
        category_order=(category,), category_blurbs={category: blurb},
        anchor=f"wdt-{side_key}",
        sort_key=_rank(side_key),
    )


# --- the sides ----------------------------------------------------------------
ROOT_SIDE = fr.Side(
    key="root", kind="chunked",
    marker="<!-- wdt-root-fields -->",
    page="python/wdt/root.md",
    module="wowlib.formats.wdt.root",
    badge_classes=("WDTRoot",), width_classes=("WDTRoot",),
    class_prefix="WDTRoot",
    repr_class="WDTRootShadowlandsPlus",
    stub="wowlib/formats/wdt/root/__init__.pyi",
    parse_fields=_fields("root"),
    categorize=lambda f: ROOT_FOURCC_CATEGORY.get(f["cc"]),
    category_order=ROOT_CATEGORY_ORDER, category_blurbs=ROOT_CATEGORY_BLURB,
    anchor="wdt-root",
    sort_key=_rank("root"),
)

OCC_SIDE = _satellite_side("occ", "WDTOcclusion", "occlusion", "WDTOcclusionWodPlus")
LGT_SIDE = _satellite_side("lgt", "WDTLights", "lights", "WDTLightsShadowlandsPlus")
FOGS_SIDE = _satellite_side("fogs", "WDTFogs", "fogs", "WDTFogsTheWarWithin")
MPV_SIDE = _satellite_side("mpv", "WDTParticulates", "mpv", "WDTParticulatesBfaPlus")

# --- the chunk-struct pages ---------------------------------------------------
# All five wire-struct modules share chunks.md; each carries its own marker.
_CPP_STRUCT_ALIAS = {"WDTHeader": "SMMapHeader"}

# Flag/enum types documented on the chunk page -> (owning chunk FourCC, the wire
# struct whose field carries the bits).
ENUM_CHUNK = {
    "MapHeaderFlags": ("MPHD", "WDTHeader"),
    "AreaInfoFlags": ("MAIN", "SMAreaInfo"),
}

CHUNKS_ROOT = fr.StructPage(
    page="python/wdt/chunks.md",
    module="wowlib.formats.wdt.root.chunks",
    stub="wowlib/formats/wdt/root/chunks.pyi",
    headers=(WDT_SRC / "root/chunks/header.hpp",),
    struct_alias=_CPP_STRUCT_ALIAS,
    owner_side=ROOT_SIDE,
    enum_chunk=ENUM_CHUNK,
    dedup_marker="<!-- wdt-chunks-root -->",
)

CHUNKS_OCC = fr.StructPage(
    page="python/wdt/chunks.md",
    module="wowlib.formats.wdt.occlusion.chunks",
    stub="wowlib/formats/wdt/occlusion/chunks.pyi",
    headers=(WDT_SRC / "occlusion/chunks/records.hpp",),
    owner_side=OCC_SIDE,
    dedup_marker="<!-- wdt-chunks-occ -->",
)

CHUNKS_LGT = fr.StructPage(
    page="python/wdt/chunks.md",
    module="wowlib.formats.wdt.lights.chunks",
    stub="wowlib/formats/wdt/lights/chunks.pyi",
    headers=(WDT_SRC / "lights/chunks/records.hpp",),
    owner_side=LGT_SIDE,
    dedup_marker="<!-- wdt-chunks-lgt -->",
)

CHUNKS_FOGS = fr.StructPage(
    page="python/wdt/chunks.md",
    module="wowlib.formats.wdt.fogs.chunks",
    stub="wowlib/formats/wdt/fogs/chunks.pyi",
    headers=(WDT_SRC / "fogs/chunks/records.hpp",),
    owner_side=FOGS_SIDE,
    dedup_marker="<!-- wdt-chunks-fogs -->",
)

CHUNKS_MPV = fr.StructPage(
    page="python/wdt/chunks.md",
    module="wowlib.formats.wdt.mpv.chunks",
    stub="wowlib/formats/wdt/mpv/chunks.pyi",
    headers=(WDT_SRC / "mpv/chunks/records.hpp",),
    owner_side=MPV_SIDE,
    dedup_marker="<!-- wdt-chunks-mpv -->",
)

FORMAT = fr.Format(
    key="wdt",
    fields_page="python/wdt/root.md",
    legend_marker="<!-- wdt-legend -->",
    sides=(ROOT_SIDE, OCC_SIDE, LGT_SIDE, FOGS_SIDE, MPV_SIDE),
    wowdev_page="WDT",
    anchors_file="wdt_wowdev_anchors.json",
    # `*?` not `+?`: the bare assembly class is "WDT"+version (WDTTheWarWithin)
    # with NO stem chars between, so a `+` would never let the suffix genericize.
    name_re=r"WDT[A-Za-z]*?",
    generic_pages=frozenset({"python/wdt/entity.md", "python/wdt/root.md",
                             "python/wdt/satellites.md", "python/wdt/chunks.md"}),
    forver={
        "python/wdt/entity.md": (
            ("wowlib.formats.wdt.WDT", "WDT", "Wotlk"),
        ),
        "python/wdt/root.md": (
            ("wowlib.formats.wdt.root.WDTRoot", "WDTRoot", "Wotlk"),
        ),
        "python/wdt/satellites.md": (
            ("wowlib.formats.wdt.occlusion.WDTOcclusion", "WDTOcclusion", "Wod"),
            ("wowlib.formats.wdt.lights.WDTLights", "WDTLights", "Wod"),
            ("wowlib.formats.wdt.fogs.WDTFogs", "WDTFogs", "Legion"),
            ("wowlib.formats.wdt.mpv.WDTParticulates", "WDTParticulates", "Bfa"),
        ),
    },
    struct_pages=(CHUNKS_ROOT, CHUNKS_OCC, CHUNKS_LGT, CHUNKS_FOGS, CHUNKS_MPV),
    # Entity families referenced from property annotations (WDT.root,
    # WDT.occlusion, WDTRoot.header) — the concrete versioned classes render
    # nowhere, so the links retarget to the family pages.
    elem_links={
        "WDT": ("python/wdt/entity.md", "wowlib.formats.wdt"),
        "WDTRoot": ("python/wdt/root.md", "wowlib.formats.wdt.root"),
        "WDTOcclusion": ("python/wdt/satellites.md", "wowlib.formats.wdt.occlusion"),
        "WDTLights": ("python/wdt/satellites.md", "wowlib.formats.wdt.lights"),
        "WDTFogs": ("python/wdt/satellites.md", "wowlib.formats.wdt.fogs"),
        "WDTParticulates": ("python/wdt/satellites.md", "wowlib.formats.wdt.mpv"),
    },
)
