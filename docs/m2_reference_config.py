"""M2 config for the format-reference engine (format_reference_impl.py).

Data + small functions only. M2 has two field surfaces ("sides") with different
kinds:

  * the **MD20 body** (`M2Root`, root/root.hpp) is an OFFSET entity — no
    per-member FourCC chunks; layout order is the struct's own declaration order
    with trait-slot members spliced in at their `=offset_after("name")` anchors,
    so its fields page section shows plain field listings with expansion
    badges only. `=since()`/`=until()` here reference the NAMED boundary
    constants from boundaries.hpp (m2_per_sequence_timelines, …), resolved
    via parse_version_constants.
  * the **Legion+ chunked shell** (`M2ChunkedFile`, chunked/chunked.hpp) is a real chunk
    stream (forward FourCCs, unlike every other WoW chunk format), so its
    members carry FourCC badges linking wowdev.wiki/M2 plus since() badges.

The record modules (root.record, chunked.record, skin, the Skel* chunk
payloads) are dumped by mkdocstrings on records.md; the engine re-annotates their layout integer widths
from the C++ headers listed here.
"""

from __future__ import annotations

import re

import format_reference_impl as fr

M2_SRC = fr.REPO_ROOT / "src/wowlib/formats/m2"
BODY_HPP = M2_SRC / "root/root.hpp"
SHELL_HPP = M2_SRC / "chunked/chunked.hpp"
BOUNDARIES_HPP = M2_SRC / "boundaries.hpp"
BUILDS_HPP = fr.REPO_ROOT / "src/wowlib/core/client_builds.hpp"
RECORD_HEADERS = tuple(
    M2_SRC / "root/record" / f"{fn}.hpp"
    for fn in ("bone", "effects", "material", "scene", "sequence", "track"))
CHUNKED_RECORDS_HPP = M2_SRC / "chunked/records.hpp"

# --- version boundary constants -----------------------------------------------
_CONSTS_CACHE: dict | None = None


def _consts() -> dict[str, tuple[int, int, int]]:
    global _CONSTS_CACHE
    if _CONSTS_CACHE is None:
        _CONSTS_CACHE = fr.parse_version_constants(BUILDS_HPP, BOUNDARIES_HPP)
    return _CONSTS_CACHE


# --- fields-page category taxonomies ------------------------------------------
# The body categories are keyed by FIELD NAME (an offset entity has no FourCCs);
# like WMO's, the grouping is documentation structure, not layout semantics. A
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
    "Header & identity": "The layout format version, the model's name and its global flags.",
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
    """M2Root members: the detail:: trait slots (version-gated) plus the struct's
    own members — the slice from `namespace detail` to end-of-file covers both.
    The excluded layout fields (magic, num_skin_profiles) never parse: their
    annotations carry welder::mark::exclude, matching their absence in Python."""
    if "body" not in _FIELDS_CACHE:
        text = fr.slice_text(BODY_HPP.read_text(encoding="utf-8"), "namespace detail", None)
        fields = {f["name"]: f for f in fr.parse_members(text, consts=_consts())}
        # The struct-head annotation block ([[=welder::weld…]] M2Root : bases…)
        # parses as a spurious member named after the first `;` inside the body
        # (the `version` constant); it is not a layout field, drop it.
        fields.pop("Version", None)
        _FIELDS_CACHE["body"] = fields
    return _FIELDS_CACHE["body"]


def _shell_fields() -> dict[str, dict]:
    """M2ChunkedFile chunk members, in declaration order (the shell has no chunk_order
    table — reading is chunk-order independent, writing follows declaration)."""
    if "shell" not in _FIELDS_CACHE:
        text = fr.slice_text(SHELL_HPP.read_text(encoding="utf-8"), "]] M2ChunkedFile :", None)
        _FIELDS_CACHE["shell"] = {f["name"]: f for f in
                                  fr.parse_members(text, consts=_consts())}
        _FIELDS_CACHE["shell"].pop("version", None)
    return _FIELDS_CACHE["shell"]


_LAYOUT_ORDER_CACHE: list[str] | None = None


def _layout_order() -> list[str]:
    """M2Root's layout order, rebuilt the way the serializer walks it: the
    struct's OWN members in declaration order, each followed by the trait
    members whose `=offset_after("name")` anchors it (see offset_block.hpp's
    member_order)."""
    global _LAYOUT_ORDER_CACHE
    if _LAYOUT_ORDER_CACHE is None:
        txt = BODY_HPP.read_text(encoding="utf-8")
        traits = fr.parse_members(
            fr.slice_text(txt, "namespace detail", "]] M2Root :"), consts=_consts())
        own = fr.parse_members(
            fr.slice_text(txt, "]] M2Root :", None), consts=_consts())
        order: list[str] = []
        for f in own:
            if f["name"] == "version":     # the struct-head parse artifact
                continue
            order.append(f["name"])
            order.extend(t["name"] for t in traits if t.get("after") == f["name"])
        _LAYOUT_ORDER_CACHE = order
    return _LAYOUT_ORDER_CACHE


def _body_rank(f) -> int:
    order = _layout_order()
    return order.index(f["name"]) if f["name"] in order else len(order)


# --- the sides ----------------------------------------------------------------
BODY_SIDE = fr.Side(
    key="body", kind="offset",
    marker="<!-- m2-root-fields -->",
    page="python/m2/root.md",
    module="wowlib.formats.m2.root",
    badge_classes=("M2Root",), width_classes=("M2Root",),
    class_prefix="M2Root",
    # Latest version = largest field set; the pre-WotLK-only members
    # (skin_profiles, playable_animation_lookup, texture_flipbooks) render from
    # the last class that has them (M2RootTbc), via the engine's removed-field
    # resolution.
    repr_class="M2RootLegionPlus",
    stub="wowlib/formats/m2/root/__init__.pyi",
    parse_fields=_body_fields,
    categorize=lambda f: BODY_FIELD_CATEGORY.get(f["name"]),
    category_order=BODY_CATEGORY_ORDER, category_blurbs=BODY_CATEGORY_BLURB,
    anchor="m2-root",
    sort_key=_body_rank,
)

SHELL_SIDE = fr.Side(
    key="shell", kind="chunked",
    marker="<!-- m2-shell-fields -->",
    page="python/m2/chunks.md",
    module="wowlib.formats.m2.chunked",
    badge_classes=("M2ChunkedFile",), width_classes=("M2ChunkedFile",),
    class_prefix="M2ChunkedFile",
    repr_class="M2ChunkedFileTheWarWithin",
    stub="wowlib/formats/m2/chunked/__init__.pyi",
    parse_fields=_shell_fields,
    categorize=lambda f: SHELL_FOURCC_CATEGORY.get(f["cc"]),
    category_order=SHELL_CATEGORY_ORDER, category_blurbs=SHELL_CATEGORY_BLURB,
    anchor="m2-shell",
    sort_key=None,                    # declaration order == chunk write order
)

# --- value-templated record families ------------------------------------------
# M2Track, FBlock, M2SplineKey and M2PartTrack are C++ templates over a value
# type T (M2Track<C4Quaternion, V>, FBlock<uint16>, …). welder mangles the value
# into the class name (M2TrackC4QuaternionWotlkPlus), so the stubs carry ~25
# near-identical classes. The docs collapse them: each base documents ONCE, its
# value member(s) shown as the generic ⟨value⟩; a reference to a concrete
# instance renders as `Base[Value]`, both parts falling through to their docs.
# Value keyed by the member(s) typed on T.
VALUE_TEMPLATES = {
    "M2Track": {"values"},
    "FBlock": {"keys"},
    "M2SplineKey": {"value", "in_tan", "out_tan"},
    "M2PartTrack": {"values"},
}
# The welded value suffix -> the type token shown in `Base[Value]`. A suffix that
# already names a documented type (C3Vector, C4Quaternion, Fixed16) maps to
# itself; the rest are spelled out — CompQuat is the M2CompQuat record, Spline*
# are M2SplineKey instances (rendered recursively), and Float/UIntN are the
# Python scalars the value collapses to.
VALUE_ALIAS = {
    "CompQuat": "M2CompQuat",
    "Fixed16": "fixed16",                    # welded name is lower-case fixed16
    "SplineC3Vector": "M2SplineKeyC3Vector",
    "SplineFloat": "M2SplineKeyFloat",
    "Float": "float", "UInt8": "int", "UInt16": "int",
}

# --- the record pages ---------------------------------------------------------
# All three record surfaces share records.md; each carries its own module id and
# int-width headers. No owner backlinks (the M2 body is offset-addressed — a
# record maps to a field, not a chunk) and no enum->chunk table.
RECORDS_BODY = fr.StructPage(
    page="python/m2/records.md",
    module="wowlib.formats.m2.root.record",
    stub="wowlib/formats/m2/root/record.pyi",
    headers=RECORD_HEADERS,
    # The event-track family is welded M2EventTrack but is the M2TrackBase<V>
    # template; the value-typed M2Track variants (M2TrackC3Vector, …) resolve
    # to M2Track by prefix, so only this rename needs spelling out.
    struct_alias={"M2EventTrack": "M2TrackBase"},
    value_templates=VALUE_TEMPLATES,
    dedup_marker="<!-- m2-records-body -->",
)

RECORDS_CHUNKED = fr.StructPage(
    page="python/m2/records.md",
    module="wowlib.formats.m2.chunked.record",
    stub="wowlib/formats/m2/chunked/record.pyi",
    headers=(CHUNKED_RECORDS_HPP,),
    dedup_marker="<!-- m2-records-chunked -->",
)

RECORDS_SKIN = fr.StructPage(
    page="python/m2/records.md",
    module="wowlib.formats.m2.skin",
    stub="wowlib/formats/m2/skin.pyi",
    headers=(M2_SRC / "skin/records.hpp",),
    # The skin stub also declares the Skin entity family (documented on
    # entities.md) — keep only the records out of it for the element registry.
    names_filter=lambda n: n.startswith("M2"),
    dedup_marker="<!-- m2-records-skin -->",
)

RECORDS_SKEL = fr.StructPage(
    page="python/m2/records.md",
    module="wowlib.formats.m2",
    stub="wowlib/formats/m2/__init__.pyi",
    headers=(M2_SRC / "skeleton.hpp",),
    # The assembly stub declares the whole M2/Skeleton families; only the SK*1
    # chunk payload records are documented on records.md ("Skel" + uppercase, so
    # the Skeleton entity family itself stays on the entities page).
    names_filter=lambda n: re.match(r"Skel[A-Z]", n) is not None,
    dedup_marker="<!-- m2-records-skel -->",
)

# Width-only pages: the entity-page members with fixed-width layout ints
# (Skeleton's FileDataID lists, the .bone file's ids/version) get the same
# Annotated[int, uintN] treatment as the record structs.
ENTITY_SKELETON = fr.StructPage(
    page="python/m2/entities.md",
    module="wowlib.formats.m2",
    stub="wowlib/formats/m2/__init__.pyi",
    headers=(M2_SRC / "skeleton.hpp",),
    names_filter=lambda n: n.startswith("Skeleton"),
)

ENTITY_BONE = fr.StructPage(
    page="python/m2/entities.md",
    module="wowlib.formats.m2.bone",
    stub="wowlib/formats/m2/bone.pyi",
    headers=(M2_SRC / "bone/bone.hpp",),
)

FORMAT = fr.Format(
    key="m2",
    # M2 splits its field surfaces onto dedicated pages (each Side carries its
    # own `page`); this stays the default for a side without one.
    fields_page="python/m2/root.md",
    legend_marker="<!-- m2-legend -->",
    sides=(BODY_SIDE, SHELL_SIDE),
    wowdev_page="M2",
    anchors_file="m2_wowdev_anchors.json",
    name_re=r"(?:M2|Skeleton|Skin|Skel|Exp2|Pabc|Psbc|Pgd1)[A-Za-z0-9]*?",
    generic_pages=frozenset({"python/m2/entities.md", "python/m2/records.md"}),
    forver={
        "python/m2/root.md": (
            ("wowlib.formats.m2.root.M2Root", "M2Root", "Wotlk"),
        ),
        "python/m2/chunks.md": (
            # The chunked shell only exists Legion+, so the example narrows to
            # a version its family actually has.
            ("wowlib.formats.m2.chunked.M2ChunkedFile", "M2ChunkedFile", "Legion"),
        ),
        "python/m2/entities.md": (
            ("wowlib.formats.m2.M2", "M2", "Wotlk"),
            ("wowlib.formats.m2.Skeleton", "Skeleton", "Legion"),
            ("wowlib.formats.m2.skin.Skin", "Skin", "Wotlk"),
        ),
    },
    struct_pages=(RECORDS_BODY, RECORDS_CHUNKED, RECORDS_SKIN, RECORDS_SKEL,
                  ENTITY_SKELETON, ENTITY_BONE),
    value_alias=VALUE_ALIAS,
    # Entity families referenced from vector annotations (M2.skins,
    # Skeleton.parent_link, …) link to their family page, not a records page.
    elem_links={
        "M2": ("python/m2/entities.md", "wowlib.formats.m2"),
        "Skeleton": ("python/m2/entities.md", "wowlib.formats.m2"),
        "Skin": ("python/m2/entities.md", "wowlib.formats.m2.skin"),
        "BoneFile": ("python/m2/entities.md", "wowlib.formats.m2.bone"),
        "BoneFilePrelude": ("python/m2/entities.md", "wowlib.formats.m2.bone"),
        "M2Root": ("python/m2/root.md", "wowlib.formats.m2.root"),
        "M2ChunkedFile": ("python/m2/chunks.md", "wowlib.formats.m2.chunked"),
    },
)
