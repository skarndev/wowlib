"""WMO config for the format-reference engine (format_reference_impl.py).

Data + small functions only — which C++ headers carry the entity members, the
fields-page category taxonomy, the representative per-version classes and the
chunk-page backlink tables. The WMO layout is version-parametric (`WMO<V>` per
expansion); the entity headers (`root/root.hpp`, `group/group.hpp`) carry, per
chunk member, its `=chunk()` FourCC and its `=since()`/`=until()` client
versions — the single source of truth the engine renders badges from.
"""

from __future__ import annotations

import re

import format_reference_impl as fr

ROOT_HPP = fr.REPO_ROOT / "src/wowlib/formats/wmo/root/root.hpp"
GROUP_HPP = fr.REPO_ROOT / "src/wowlib/formats/wmo/group/group.hpp"
CHUNK_DIR = fr.REPO_ROOT / "src/wowlib/formats/wmo"

# --- fields-page category taxonomy -------------------------------------------
# Chunks are grouped into these sections on the fields page (finer-grained split:
# Collision apart from Geometry, Volumes from Fog, Group-table from Culling). This
# grouping is pure documentation structure, not wire semantics, so it lives here
# rather than as a C++ annotation; a chunk FourCC missing from FOURCC_CATEGORY is
# logged and dropped into "Header" so the omission is visible, never silent.
CATEGORY_ORDER = ("Header", "Materials", "Geometry", "Collision", "Doodads",
                  "Lights", "Portals", "Fog", "Volumes", "Liquid", "Group table",
                  "Culling", "Skybox")
FOURCC_CATEGORY = {
    "MVER": "Header", "MOHD": "Header", "MOGP": "Header",
    "MOMT": "Materials", "MOM3": "Materials", "MOTX": "Materials", "MOUV": "Materials",
    "MOPY": "Geometry", "MPY2": "Geometry", "MOVI": "Geometry", "MOVX": "Geometry",
    "MOVT": "Geometry", "MONR": "Geometry", "MOTV": "Geometry", "MOBA": "Geometry",
    "MORB": "Geometry", "MOTA": "Geometry", "MOBS": "Geometry", "MOCV": "Geometry",
    "MOC2": "Geometry", "MORI": "Geometry", "MOGX": "Geometry", "MOQG": "Geometry",
    "MOBN": "Collision", "MOBR": "Collision", "MOPB": "Geometry",
    "MDAL": "Lights", "MOPL": "Portals",
    "MODS": "Doodads", "MODN": "Doodads", "MODI": "Doodads", "MODD": "Doodads",
    "MDDI": "Doodads", "MDDL": "Doodads", "MODR": "Doodads",
    "MOLT": "Lights", "MOLV": "Lights", "MNLD": "Lights", "MOLR": "Lights",
    "MOLP": "Lights", "MOLS": "Lights", "MLSS": "Lights", "MLSP": "Lights",
    "MLSK": "Lights", "MOP2": "Lights", "MNLR": "Lights",
    "MOPV": "Portals", "MOPT": "Portals", "MOPR": "Portals", "MOPE": "Portals",
    "MFOG": "Fog", "MFED": "Fog", "MFVR": "Fog",
    "MPVD": "Volumes", "MAVG": "Volumes", "MAVD": "Volumes", "MBVD": "Volumes",
    "MPVR": "Volumes", "MAVR": "Volumes", "MBVR": "Volumes",
    "MLIQ": "Liquid",
    "MOGN": "Group table", "MOGI": "Group table", "MGI2": "Group table", "GFID": "Group table",
    "MOVV": "Culling", "MOVB": "Culling", "MCVP": "Culling", "MOMX": "Culling",
    "MOSB": "Skybox", "MOSI": "Skybox",
}
CATEGORY_BLURB = {
    "Header": "The file version and the root/group header.",
    "Materials": "The material table, shaders and texture references.",
    "Geometry": "Vertices, normals, texture coordinates, render batches and triangle data.",
    "Collision": "The collision BSP tree.",
    "Doodads": "Doodad (M2) sets, placements and their file references.",
    "Lights": "Static and dynamic lights, and the group references into them.",
    "Portals": "Portal geometry linking groups for visibility.",
    "Fog": "Fog volumes and the group references into them.",
    "Volumes": "Particulate and ambient volumes, and the group references into them.",
    "Liquid": "The liquid grid (water, lava, slime).",
    "Group table": "Group names, per-group info and the group-file table.",
    "Culling": "Visibility blocks and convex culling volumes.",
    "Skybox": "The skybox reference.",
}

# Flag/enum types documented on the chunk pages -> (owning chunk FourCC, the wire
# struct whose field carries the bits). The FourCC drives the wowdev badge (that
# chunk's section); the struct drives a same-page backlink to where it is used
# (e.g. PolyFlags -> SMOPoly.flags). LightType is a plain enum, treated the same.
ENUM_CHUNK = {
    "MaterialFlags": ("MOMT", "SMOMaterial"),
    "HeaderFlags": ("MOHD", "SMOHeader"),
    "LightType": ("MOLT", "SMOLight"),
    "DoodadFlags": ("MODD", "SMODoodadDef"),
    "PolyFlags": ("MOPY", "SMOPoly"),
    "GroupFlags": ("MOGP", "WMOGroupHeader"),
    "GroupFlags2": ("MOGP", "WMOGroupHeader"),
}

# The chunk wire-struct headers per side (int-width source), and the templated
# structs that weld under a different Python name than their C++ struct (their
# per-version classes share one field layout).
_CHUNK_HEADERS = {
    "root": ("doodad", "environment", "header", "light", "material", "structure"),
    "group": ("geometry", "header", "light", "liquid"),
}
_CPP_STRUCT_ALIAS = {"WMOGroupHeader": "SMOGroupHeader", "WMOBatch": "SMOBatch"}


# --- entity member parsing ----------------------------------------------------
_FIELDS_CACHE: tuple[dict, dict] | None = None


def _entity_fields() -> tuple[dict, dict]:
    """(root_fields, group_fields): field name -> parsed info. Group merges the
    MOGP body members with the WMOGroup wrapper's (mver)."""
    global _FIELDS_CACHE
    if _FIELDS_CACHE is None:
        # WMORoot's version-gated chunks live in detail:: trait bases declared ahead
        # of the struct, so slice from the detail namespace (traits) to end-of-file,
        # covering the traits + WMORoot's own members.
        root = {f["name"]: f for f in fr.parse_members(
            fr.slice_text(ROOT_HPP.read_text(encoding="utf-8"), "namespace detail", None),
            header_cc="MOGP")}
        gtxt = GROUP_HPP.read_text(encoding="utf-8")
        # WMOGroupBody's version-gated chunks live in detail:: trait bases declared
        # ahead of the struct, so slice from the detail namespace (traits) through
        # WMOGroupBody's own members, up to the WMOGroup wrapper.
        group = {f["name"]: f for f in fr.parse_members(
            fr.slice_text(gtxt, "namespace detail", "]] WMOGroup :"), header_cc="MOGP")}
        for f in fr.parse_members(fr.slice_text(gtxt, "]] WMOGroup :", None),
                                  header_cc="MOGP"):
            group.setdefault(f["name"], f)
        _FIELDS_CACHE = (root, group)
    return _FIELDS_CACHE


_ORDER_CACHE: dict[str, list[str]] = {}


def _chunk_order(side_key: str) -> list[str]:
    """The entity's canonical chunk FourCC order (its `static constexpr chunk_order`
    table), so fields render in stream order rather than trait-flatten order."""
    if side_key not in _ORDER_CACHE:
        txt = (ROOT_HPP if side_key == "root" else GROUP_HPP).read_text(encoding="utf-8")
        m = re.search(r"chunk_order\s*=\s*\{(.*?)\}", txt, re.DOTALL)
        _ORDER_CACHE[side_key] = re.findall(r'four_cc\("([^"]+)"\)', m.group(1)) if m else []
    return _ORDER_CACHE[side_key]


def _rank(side_key: str):
    def key(f):
        order = _chunk_order(side_key)
        return order.index(f["cc"]) if f["cc"] in order else len(order)
    return key


def _categorize(f) -> str | None:
    return FOURCC_CATEGORY.get(f["cc"])


# --- the sides ----------------------------------------------------------------
ROOT_SIDE = fr.Side(
    key="root", kind="chunked",
    marker="<!-- wmo-root-fields -->",
    module="wowlib.formats.wmo.root",
    badge_classes=("WMORoot",), width_classes=("WMORoot",),
    class_prefix="WMORoot",
    # The concrete representative class the side's fields are documented on. Since
    # a per-version type only carries its version's fields, the representative must
    # be the one with the largest field set: the latest version. Chunks REMOVED
    # before then (e.g. root's MOSB, gone at 8.1) are absent from it — the engine
    # completes the superset by introspecting every version class and rendering
    # whatever the representative lacks from the last class that has it.
    repr_class="WMORootTheWarWithin",
    stub="wowlib/formats/wmo/root/__init__.pyi",
    parse_fields=lambda: _entity_fields()[0],
    categorize=_categorize,
    category_order=CATEGORY_ORDER, category_blurbs=CATEGORY_BLURB,
    anchor="wmo-root",
    sort_key=_rank("root"),
)

GROUP_SIDE = fr.Side(
    key="group", kind="chunked",
    marker="<!-- wmo-group-fields -->",
    module="wowlib.formats.wmo.group",
    badge_classes=("WMOGroupBody",),
    width_classes=("WMOGroupBody", "WMOGroup"),   # the WMOGroup wrapper's mver too
    class_prefix="WMOGroupBody",
    repr_class="WMOGroupBodyDragonflightPlus",
    stub="wowlib/formats/wmo/group/__init__.pyi",
    parse_fields=lambda: _entity_fields()[1],
    categorize=_categorize,
    category_order=CATEGORY_ORDER, category_blurbs=CATEGORY_BLURB,
    anchor="wmo-group",
    sort_key=_rank("group"),
)

# --- the chunk-struct pages ---------------------------------------------------
ROOT_CHUNKS = fr.StructPage(
    page="python/wmo/root-chunks.md",
    module="wowlib.formats.wmo.root.chunks",
    stub="wowlib/formats/wmo/root/chunks.pyi",
    headers=tuple(CHUNK_DIR / "root/chunks" / f"{fn}.hpp" for fn in _CHUNK_HEADERS["root"]),
    struct_alias=_CPP_STRUCT_ALIAS,
    owner_side=ROOT_SIDE,
    enum_chunk=ENUM_CHUNK,
)

GROUP_CHUNKS = fr.StructPage(
    page="python/wmo/group-chunks.md",
    module="wowlib.formats.wmo.group.chunks",
    stub="wowlib/formats/wmo/group/chunks.pyi",
    headers=tuple(CHUNK_DIR / "group/chunks" / f"{fn}.hpp" for fn in _CHUNK_HEADERS["group"]),
    struct_alias=_CPP_STRUCT_ALIAS,
    owner_side=GROUP_SIDE,
    enum_chunk=ENUM_CHUNK,
)

FORMAT = fr.Format(
    key="wmo",
    fields_page="python/wmo/fields.md",
    legend_marker="<!-- wmo-legend -->",
    sides=(ROOT_SIDE, GROUP_SIDE),
    wowdev_page="WMO",
    anchors_file="wmo_wowdev_anchors.json",
    name_re=r"WMO[A-Za-z]+?",
    generic_pages=frozenset({"python/wmo/fields.md"}),
    forver={
        "python/wmo/fields.md": (
            ("wowlib.formats.wmo.root.WMORoot", "WMORoot", "Wotlk"),
            ("wowlib.formats.wmo.group.WMOGroup", "WMOGroup", "Wotlk"),
            ("wowlib.formats.wmo.group.WMOGroupBody", "WMOGroupBody", "Wotlk"),
        ),
    },
    struct_pages=(ROOT_CHUNKS, GROUP_CHUNKS),
)
