"""M2 config for the format-reference engine (format_reference_impl.py).

Data + small functions only. M2 has two field surfaces ("sides") with different
kinds:

  * the **MD20 body** (`M2Data`, body/data.hpp) is an OFFSET entity — no
    per-member FourCC chunks; the authoritative `wire_order` array fixes the
    member positions (version-gated members live in detail:: trait slots), so
    its fields page section shows plain field listings with expansion badges
    only. `=since()`/`=until()` here reference the NAMED boundary constants
    from boundaries.hpp (m2_per_sequence_timelines, …), resolved via
    parse_version_constants.
  * the **Legion+ chunked shell** (`M2File`, body/shell.hpp) is a real chunk
    stream (forward FourCCs, unlike every other WoW chunk format), so its
    members carry FourCC badges linking wowdev.wiki/M2 plus since() badges.

The record modules (body.records, skin, the Skel* chunk payloads) are dumped by
mkdocstrings on records.md; the engine re-annotates their wire integer widths
from the C++ headers listed here.
"""

from __future__ import annotations

import re

import format_reference_impl as fr

M2_SRC = fr.REPO_ROOT / "src/wowlib/formats/m2"
BODY_HPP = M2_SRC / "body/data.hpp"
SHELL_HPP = M2_SRC / "body/shell.hpp"
BOUNDARIES_HPP = M2_SRC / "boundaries.hpp"
RECORD_HEADERS = tuple(
    M2_SRC / "body/records" / f"{fn}.hpp"
    for fn in ("bone", "effects", "material", "scene", "sequence", "shell", "track"))

# --- version boundary constants -----------------------------------------------
_CONSTS_CACHE: dict | None = None


def _consts() -> dict[str, tuple[int, int, int]]:
    global _CONSTS_CACHE
    if _CONSTS_CACHE is None:
        _CONSTS_CACHE = fr.parse_version_constants(BOUNDARIES_HPP)
    return _CONSTS_CACHE


# --- fields-page category taxonomies ------------------------------------------
# The body categories are keyed by FIELD NAME (an offset entity has no FourCCs);
# like WMO's, the grouping is documentation structure, not wire semantics. A
# field missing from the map is logged and dropped into the first category so
# the omission is visible, never silent.
BODY_CATEGORY_ORDER = ("Header & identity", "Sequences & animation", "Bones",
                       "Geometry", "Textures & materials", "Lookups",
                       "Bounds & collision", "Attachments & events",
                       "Lights & cameras", "Particles & ribbons")
BODY_FIELD_CATEGORY = {
    "format_version": "Header & identity", "name": "Header & identity",
    "global_flags": "Header & identity",
    "global_loops": "Sequences & animation", "sequences": "Sequences & animation",
    "sequence_lookups": "Sequences & animation",
    "playable_animation_lookup": "Sequences & animation",
    "bones": "Bones", "key_bone_lookup": "Bones",
    "vertices": "Geometry", "skin_profiles": "Geometry",
    "colors": "Textures & materials",
    "textures": "Textures & materials", "texture_weights": "Textures & materials",
    "texture_flipbooks": "Textures & materials",
    "texture_transforms": "Textures & materials", "materials": "Textures & materials",
    "texture_combiner_combos": "Textures & materials",
    "replacable_texture_lookup": "Lookups", "bone_lookup_table": "Lookups",
    "texture_lookup_table": "Lookups", "texture_mapping_lookup_table": "Lookups",
    "transparency_lookup_table": "Lookups",
    "texture_transforms_lookup_table": "Lookups",
    "bounding_box": "Bounds & collision", "bounding_sphere_radius": "Bounds & collision",
    "collision_box": "Bounds & collision", "collision_sphere_radius": "Bounds & collision",
    "collision_triangles": "Bounds & collision", "collision_vertices": "Bounds & collision",
    "collision_normals": "Bounds & collision",
    "attachments": "Attachments & events", "attachment_lookup_table": "Attachments & events",
    "events": "Attachments & events",
    "lights": "Lights & cameras", "cameras": "Lights & cameras",
    "camera_lookup_table": "Lights & cameras",
    "ribbon_emitters": "Particles & ribbons", "particle_emitters": "Particles & ribbons",
}
BODY_CATEGORY_BLURB = {
    "Header & identity": "The wire format version, the model's name and its global flags.",
    "Sequences & animation": "The animation sequences, global loops and the "
                             "animation-id lookup tables.",
    "Bones": "The bone hierarchy and the key-bone lookup.",
    "Geometry": "The global vertex list and the pre-WotLK embedded LOD views.",
    "Textures & materials": "Texture definitions, UV animations, color/alpha and "
                            "transparency animations, and the materials.",
    "Lookups": "The indirection tables render batches select bones, textures and "
               "animations through.",
    "Bounds & collision": "The render/collision bounds and the collision hull.",
    "Attachments & events": "Attachment points and timed events.",
    "Lights & cameras": "Model lights and cameras, with their lookups.",
    "Particles & ribbons": "Ribbon (trail) and particle emitters.",
}

# The shell categories are keyed by chunk FourCC, like WMO's.
SHELL_CATEGORY_ORDER = ("Model image", "Satellite files", "Particles",
                        "Parent model", "Render extensions")
SHELL_FOURCC_CATEGORY = {
    "MD21": "Model image",
    "PFID": "Satellite files", "SFID": "Satellite files", "AFID": "Satellite files",
    "BFID": "Satellite files", "SKID": "Satellite files", "TXID": "Satellite files",
    "RPID": "Satellite files", "GPID": "Satellite files",
    "TXAC": "Particles", "EXPT": "Particles", "EXP2": "Particles", "PGD1": "Particles",
    "PABC": "Parent model", "PADC": "Parent model", "PSBC": "Parent model",
    "PEDC": "Parent model",
    "LDV1": "Render extensions", "WFV1": "Render extensions", "WFV2": "Render extensions",
    "WFV3": "Render extensions", "PFDC": "Render extensions", "EDGF": "Render extensions",
    "NERF": "Render extensions", "DETL": "Render extensions", "DBOC": "Render extensions",
    "AFRA": "Render extensions", "PCOL": "Render extensions", "DPIV": "Render extensions",
}
SHELL_CATEGORY_BLURB = {
    "Model image": "The MD20 image the offsets address — the whole pre-Legion "
                   "model, moved into a chunk.",
    "Satellite files": "FileDataID references to the model's satellite files "
                       "(.phys, .skin, .anim, .bone, .skel, textures, child "
                       "particle models).",
    "Particles": "Extended per-emitter particle parameters.",
    "Parent model": "Overrides applied when this model extends a parent model.",
    "Render extensions": "LOD, waterfall shading, edge fade, alpha attenuation, "
                         "inline physics and the still-undocumented tails.",
}


# --- entity member parsing ----------------------------------------------------
_FIELDS_CACHE: dict[str, dict] = {}


def _body_fields() -> dict[str, dict]:
    """M2Data members: the detail:: trait slots (version-gated) plus the struct's
    own members — the slice from `namespace detail` to end-of-file covers both.
    The excluded wire fields (magic, num_skin_profiles) never parse: their
    annotations carry welder::mark::exclude, matching their absence in Python."""
    if "body" not in _FIELDS_CACHE:
        text = fr.slice_text(BODY_HPP.read_text(encoding="utf-8"), "namespace detail", None)
        fields = {f["name"]: f for f in fr.parse_members(text, consts=_consts())}
        # The struct-head annotation block ([[=welder::weld…]] M2Data : bases…)
        # parses as a spurious member named after the first `;` inside the body
        # (the `version` constant); it is not a wire field, drop it.
        fields.pop("version", None)
        _FIELDS_CACHE["body"] = fields
    return _FIELDS_CACHE["body"]


def _shell_fields() -> dict[str, dict]:
    """M2File chunk members, in declaration order (the shell has no chunk_order
    table — reading is chunk-order independent, writing follows declaration)."""
    if "shell" not in _FIELDS_CACHE:
        text = fr.slice_text(SHELL_HPP.read_text(encoding="utf-8"), "]] M2File :", None)
        _FIELDS_CACHE["shell"] = {f["name"]: f for f in
                                  fr.parse_members(text, consts=_consts())}
        _FIELDS_CACHE["shell"].pop("version", None)
    return _FIELDS_CACHE["shell"]


_WIRE_ORDER_CACHE: list[str] | None = None


def _wire_order() -> list[str]:
    """M2Data's authoritative `wire_order` member-name array (the offset-entity
    equivalent of a chunk_order table)."""
    global _WIRE_ORDER_CACHE
    if _WIRE_ORDER_CACHE is None:
        txt = BODY_HPP.read_text(encoding="utf-8")
        m = re.search(r"wire_order\s*\{(.*?)\}", txt, re.DOTALL)
        _WIRE_ORDER_CACHE = re.findall(r'"([^"]+)"', m.group(1)) if m else []
    return _WIRE_ORDER_CACHE


def _body_rank(f) -> int:
    order = _wire_order()
    return order.index(f["name"]) if f["name"] in order else len(order)


# --- the sides ----------------------------------------------------------------
BODY_SIDE = fr.Side(
    key="body", kind="offset",
    marker="<!-- m2-body-fields -->",
    module="wowlib.formats.m2.body",
    badge_classes=("M2Data",), width_classes=("M2Data",),
    class_prefix="M2Data",
    # Latest version = largest field set; the pre-WotLK-only members
    # (skin_profiles, playable_animation_lookup, texture_flipbooks) render from
    # the last class that has them (M2DataTbc), via the engine's removed-field
    # resolution.
    repr_class="M2DataLegionPlus",
    stub="wowlib/formats/m2/body/__init__.pyi",
    parse_fields=_body_fields,
    categorize=lambda f: BODY_FIELD_CATEGORY.get(f["name"]),
    category_order=BODY_CATEGORY_ORDER, category_blurbs=BODY_CATEGORY_BLURB,
    anchor="m2-body",
    sort_key=_body_rank,
)

SHELL_SIDE = fr.Side(
    key="shell", kind="chunked",
    marker="<!-- m2-shell-fields -->",
    module="wowlib.formats.m2.body",
    badge_classes=("M2File",), width_classes=("M2File",),
    class_prefix="M2File",
    repr_class="M2FileTheWarWithin",
    stub="wowlib/formats/m2/body/__init__.pyi",
    parse_fields=_shell_fields,
    categorize=lambda f: SHELL_FOURCC_CATEGORY.get(f["cc"]),
    category_order=SHELL_CATEGORY_ORDER, category_blurbs=SHELL_CATEGORY_BLURB,
    anchor="m2-shell",
    sort_key=None,                    # declaration order == chunk write order
)

# --- the record pages ---------------------------------------------------------
# All three record surfaces share records.md; each carries its own module id and
# int-width headers. No owner backlinks (the M2 body is offset-addressed — a
# record maps to a field, not a chunk) and no enum->chunk table.
RECORDS_BODY = fr.StructPage(
    page="python/m2/records.md",
    module="wowlib.formats.m2.body.records",
    stub="wowlib/formats/m2/body/records.pyi",
    headers=RECORD_HEADERS,
)

RECORDS_SKIN = fr.StructPage(
    page="python/m2/records.md",
    module="wowlib.formats.m2.skin",
    stub="wowlib/formats/m2/skin.pyi",
    headers=(M2_SRC / "skin/records.hpp",),
    # The skin stub also declares the Skin entity family (documented on
    # entities.md) — keep only the records out of it for the element registry.
    names_filter=lambda n: n.startswith("M2"),
)

RECORDS_SKEL = fr.StructPage(
    page="python/m2/records.md",
    module="wowlib.formats.m2",
    stub="wowlib/formats/m2/__init__.pyi",
    headers=(M2_SRC / "skeleton.hpp",),
    # The assembly stub declares the whole M2/Skeleton families; only the SK*1
    # chunk payload records are documented on records.md.
    names_filter=lambda n: n.startswith("Skel"),
)

FORMAT = fr.Format(
    key="m2",
    fields_page="python/m2/fields.md",
    legend_marker="<!-- m2-legend -->",
    sides=(BODY_SIDE, SHELL_SIDE),
    wowdev_page="M2",
    anchors_file="m2_wowdev_anchors.json",
    name_re=r"(?:M2|Skeleton|Skin|Skel)[A-Za-z0-9]*?",
    generic_pages=frozenset({"python/m2/fields.md", "python/m2/entities.md"}),
    forver={
        "python/m2/fields.md": (
            ("wowlib.formats.m2.body.M2Data", "M2Data", "Wotlk"),
            # The chunked shell only exists Legion+, so the example narrows to
            # a version its family actually has.
            ("wowlib.formats.m2.body.M2File", "M2File", "Legion"),
        ),
        "python/m2/entities.md": (
            ("wowlib.formats.m2.M2", "M2", "Wotlk"),
            ("wowlib.formats.m2.Skeleton", "Skeleton", "Legion"),
            ("wowlib.formats.m2.skin.Skin", "Skin", "Wotlk"),
        ),
    },
    struct_pages=(RECORDS_BODY, RECORDS_SKIN, RECORDS_SKEL),
    # Entity families referenced from vector annotations (M2.skins,
    # Skeleton.parent_link, …) link to their family page, not a records page.
    elem_links={
        "M2": ("python/m2/entities.md", "wowlib.formats.m2"),
        "Skeleton": ("python/m2/entities.md", "wowlib.formats.m2"),
        "Skin": ("python/m2/entities.md", "wowlib.formats.m2.skin"),
        "BoneFile": ("python/m2/entities.md", "wowlib.formats.m2.bone"),
        "BoneFilePrelude": ("python/m2/entities.md", "wowlib.formats.m2.bone"),
        "M2Data": ("python/m2/fields.md", "wowlib.formats.m2.body"),
        "M2File": ("python/m2/fields.md", "wowlib.formats.m2.body"),
    },
)
