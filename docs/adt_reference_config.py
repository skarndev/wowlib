"""ADT config for the format-reference engine (format_reference_impl.py).

ADT gets the same per-version field treatment as the other chunked formats
(FourCC badges + expansion-range badges on every field section), with one
twist: the `ADT<V>` tile and `MapChunk<V>` terrain-chunk entities carry NO
`=chunk()`/`=since()` member annotations (sub-chunks are hand-serialized and
version-gating is by conditional trait bases), so the per-field ranges are
derived **from the stubs** — which per-version classes declare a member — while
the FourCCs, categories and the rare mid-expansion boundary the class grid
cannot express (MDID/MHID at 8.1, MTXP at MoP) come from the hand maps below.
Two sides: the tile (`python/adt/entity.md`) and the terrain chunk / MCNK
(`python/adt/map-chunk.md`). The wire structs on `python/adt/chunks.md` get
int widths + owner backlinks; the structured-liquid records stay on the entity
page.
"""

from __future__ import annotations

import format_reference_impl as fr

ADT_SRC = fr.REPO_ROOT / "src/wowlib/formats/adt"
_MODULE = "wowlib.formats.adt"
_STUB = "wowlib/formats/adt/__init__.pyi"

# field -> (FourCC, category[, since-override]). The FourCC badge links the
# wowdev section; "" = no owning sub-chunk (bookkeeping fields). A third tuple
# element overrides the stub-derived `since` where the real boundary is
# mid-expansion (the stub grid only knows whole class ranges).
_TILE_META = {
    "mver": ("MVER", "Header & serialization"),
    "header": ("MHDR", "Header & serialization"),
    "alpha_format": ("", "Header & serialization"),
    "chunks": ("MCNK", "Terrain & water"),
    "water": ("MH2O", "Terrain & water"),
    "flying_bounds": ("MFBO", "Terrain & water"),
    "textures": ("MTEX", "Textures"),
    "texture_flags": ("MTXF", "Textures"),
    "texture_params": ("MTXP", "Textures", (5, 0, 0)),      # MoP+, class range is Cata+
    "mamp": ("MAMP", "Textures"),
    "uses_texture_fdids": ("", "Textures"),
    "diffuse_texture_ids": ("MDID", "Textures", (8, 1, 0)),  # 8.1, not 8.0
    "height_texture_ids": ("MHID", "Textures", (8, 1, 0)),
    "model_filenames": ("MMDX", "Models & placements"),
    "model_name_offsets": ("MMID", "Models & placements"),
    "wmo_filenames": ("MWMO", "Models & placements"),
    "wmo_name_offsets": ("MWID", "Models & placements"),
    "doodad_placements": ("MDDF", "Models & placements"),
    "wmo_placements": ("MODF", "Models & placements"),
}

_CHUNK_META = {
    "header": ("MCNK", "Header"),
    "heights": ("MCVT", "Surface"),
    "normals": ("MCNR", "Surface"),
    "vertex_colors": ("MCCV", "Surface"),
    "vertex_lighting": ("MCLV", "Surface"),
    "layers": ("MCLY", "Texturing"),
    "alpha_maps": ("MCAL", "Texturing"),
    "shadow_map": ("MCSH", "Texturing"),
    "material_ids": ("MCMT", "Texturing"),
    "doodad_refs": ("MCRF", "References & effects"),
    "object_refs": ("MCRF", "References & effects"),
    "sound_emitters": ("MCSE", "References & effects"),
    "legacy_liquid": ("MCLQ", "Liquid"),
}


def _elem_of(annotation: str) -> str:
    """The element/type identifier of a stub property annotation:
    ``wowlib.VectorSMLayer`` -> SMLayer (via the opaque-vector registry),
    ``chunks.MHDRData`` -> MHDRData, ``int`` -> int."""
    tail = annotation.split(".")[-1].strip()
    return fr._vector_elements().get(tail, tail)


def _stub_fields(prefix: str, meta: dict[str, tuple]) -> dict[str, dict]:
    """A Side.parse_fields over the per-version stub classes of one family
    (``ADT``/``MapChunk``): which classes declare a member becomes its
    since/until (whole-expansion resolution), FourCC/category/overrides come
    from ``meta``. Field order: the newest class's declaration order, with
    older-only members spliced in after their predecessor."""
    fams: list[tuple[int, str, dict]] = []
    for name, block in fr._stub_class_blocks(_STUB):
        split = fr.split_range_suffix(name)
        if not split or split[0] != prefix:
            continue
        fams.append((fr.range_rank(split[1]), split[1], fr._class_props(block)))
    fams.sort(key=lambda t: t[0])
    if not fams:
        return {}
    order = list(fams[-1][2].keys())
    for _rank, _suffix, props in reversed(fams[:-1]):
        prev = None
        for name in props:
            if name in order:
                prev = name
                continue
            order.insert(order.index(prev) + 1 if prev is not None else 0, name)
            prev = name
    out: dict[str, dict] = {}
    for name in order:
        spans = [fr._suffix_span(suffix)
                 for _rank, suffix, props in fams if name in props]
        first, last = spans[0][0], spans[-1][1]
        entry = meta.get(name, ("", None))
        cc, category = entry[0], entry[1]
        since = entry[2] if len(entry) > 2 else (
            (first, 0, 0) if first > 1 else None)
        newest = next(props for _rank, _suffix, props in reversed(fams)
                      if name in props)
        out[name] = {
            "name": name, "cc": cc, "category": category,
            "since": since,
            "until": None if last is None else (last + 1, 0, 0),
            "elem": _elem_of(newest[name][0]),
            "int_width": None, "container": False, "after": None,
        }
    return out


TILE_SIDE = fr.Side(
    key="tile",
    kind="chunked",
    marker="<!-- adt-tile-fields -->",
    module=_MODULE,
    badge_classes=("ADT",),
    width_classes=("ADT",),
    class_prefix="ADT",
    repr_class="ADTBfaPlus",
    stub=_STUB,
    parse_fields=lambda: _stub_fields("ADT", _TILE_META),
    categorize=lambda f: f.get("category"),
    category_order=("Header & serialization", "Terrain & water", "Textures",
                    "Models & placements"),
    category_blurbs={
        "Header & serialization":
            "The format version, the MHDR header and how this tile's alpha "
            "maps were encoded on disk (offsets are re-derived on write).",
        "Terrain & water":
            "The 256 terrain chunks and the tile-wide water and flying bounds.",
        "Textures":
            "The tileset texture table the chunk layers index — filenames "
            "pre-8.1, `_s.blp`/`_h.blp` FileDataID pairs on 8.1+ "
            "height-texturing maps — plus the per-texture parameter tables.",
        "Models & placements":
            "The M2/WMO filename tables and the doodad/WMO placements chunks "
            "reference into.",
    },
    anchor="adt-tile",
    page="python/adt/entity.md",
)

CHUNK_SIDE = fr.Side(
    key="chunk",
    kind="chunked",
    marker="<!-- adt-chunk-fields -->",
    module=_MODULE,
    badge_classes=("MapChunk",),
    width_classes=("MapChunk",),
    class_prefix="MapChunk",
    repr_class="MapChunkCataPlus",
    stub=_STUB,
    parse_fields=lambda: _stub_fields("MapChunk", _CHUNK_META),
    categorize=lambda f: f.get("category"),
    category_order=("Header", "Surface", "Texturing", "References & effects",
                    "Liquid"),
    category_blurbs={
        "Header": "The MCNK header record (flags, grid position, area, holes).",
        "Surface":
            "The 9 × 9 + 8 × 8 = 145 vertex grids: heights, normals and the "
            "version-gated per-vertex colours and baked lighting.",
        "Texturing":
            "The texture layers with their decoded 64 × 64 alpha maps, the "
            "shadow map and the Cata+ terrain materials.",
        "References & effects":
            "Which of the tile's placements draw in this chunk, and its sound "
            "emitters.",
        "Liquid":
            "The legacy per-chunk MCLQ water (superseded by the tile-wide "
            "MH2O since Cataclysm — see the entity page's structured liquid).",
    },
    anchor="adt-chunk",
    page="python/adt/map-chunk.md",
)

# --- wire-struct pages ----------------------------------------------------
# chunks.md is split into three StructPages over the same module so each wire
# struct can backlink the entity field that uses it (owner_side is single):
# tile-owned records, chunk-owned records, and the liquid vertex records
# (owned by LiquidInstance on the entity page — no side to backlink).
_HEADERS = (ADT_SRC / "chunks/header.hpp", ADT_SRC / "chunks/texture.hpp",
            ADT_SRC / "chunks/liquid.hpp", ADT_SRC / "chunks/object.hpp")

_TILE_STRUCTS = {"MapHeaderFlags", "MHDRData", "MFBOPlanes", "SMTextureFlags",
                 "SMTextureParams", "SMTextureColorGrading", "SMDoodadSetRange"}
_CHUNK_STRUCTS = {"MapChunkFlags", "SMChunk", "LayerFlags", "SMLayer",
                  "SMTerrainMaterial", "MCNREntry", "CWSoundEmitter"}

CHUNKS_TILE = fr.StructPage(
    page="python/adt/chunks.md",
    module="wowlib.formats.adt.chunks",
    stub="wowlib/formats/adt/chunks.pyi",
    headers=_HEADERS,
    names_filter=lambda n: n in _TILE_STRUCTS,
    owner_side=TILE_SIDE,
    enum_chunk={"MapHeaderFlags": ("MHDR", "MHDRData")},
)

CHUNKS_CHUNK = fr.StructPage(
    page="python/adt/chunks.md",
    module="wowlib.formats.adt.chunks",
    stub="wowlib/formats/adt/chunks.pyi",
    headers=_HEADERS,
    names_filter=lambda n: n in _CHUNK_STRUCTS,
    owner_side=CHUNK_SIDE,
    enum_chunk={"MapChunkFlags": ("MCNK", "SMChunk"),
                "LayerFlags": ("MCLY", "SMLayer")},
)

CHUNKS_LIQUID = fr.StructPage(
    page="python/adt/chunks.md",
    module="wowlib.formats.adt.chunks",
    stub="wowlib/formats/adt/chunks.pyi",
    headers=_HEADERS,
    names_filter=lambda n: n not in (_TILE_STRUCTS | _CHUNK_STRUCTS),
    enum_chunk={"LiquidVertexFormat": ("MH2O", "")},
)

# The structured liquid welded at the adt module level; MH2OData backlinks the
# tile's `water` field (the rest are its own nested records).
LIQUID = fr.StructPage(
    page="python/adt/entity.md",
    module=_MODULE,
    stub=_STUB,
    headers=(ADT_SRC / "liquid.hpp",),
    names_filter=lambda n: n in {"LiquidInstance", "MapChunkLiquid", "MH2OData",
                                 "MCLQData"},
    owner_side=TILE_SIDE,
)

FORMAT = fr.Format(
    key="adt",
    fields_page="python/adt/entity.md",
    legend_marker="<!-- adt-legend -->",
    sides=(TILE_SIDE, CHUNK_SIDE),
    wowdev_page="ADT/v18",
    anchors_file="adt_wowdev_anchors.json",
    name_re=r"ADT|MapChunk",
    generic_pages=frozenset(),
    forver={
        "python/adt/entity.md": (
            ("wowlib.formats.adt.ADT", "ADT", "Wotlk"),
        ),
        "python/adt/map-chunk.md": (
            ("wowlib.formats.adt.MapChunk", "MapChunk", "Wotlk"),
        ),
    },
    struct_pages=(CHUNKS_TILE, CHUNKS_CHUNK, CHUNKS_LIQUID, LIQUID),
    elem_links={
        "ADT": ("python/adt/entity.md", _MODULE),
        "MapChunk": ("python/adt/map-chunk.md", _MODULE),
    },
)
