"""ADT config for the format-reference engine (format_reference_impl.py).

ADT is the odd one out: its `ADT<V>` tile and `MapChunk<V>` cell entities carry
NO `=chunk()` annotations (the sub-chunks are hand-serialized, and version-gating
is by conditional trait bases, not member since/until), so the FourCC field-badge
engine the other formats use does not apply. The entity and cell classes are
therefore documented on plain mkdocstrings pages (python/adt/{entity,cells}.md),
and this config only registers WIDTH-ONLY StructPages for the wire structs and
the structured-liquid records — the same minimal shape the `common` config uses,
so every engine pass but the wire int-width one no-ops here.
"""

from __future__ import annotations

import format_reference_impl as fr

ADT_SRC = fr.REPO_ROOT / "src/wowlib/formats/adt"

# The trivially-copyable wire structs: the MCNK cell header, texture layers,
# liquid vertex records, flying bounds, sound emitters.
CHUNKS = fr.StructPage(
    page="python/adt/chunks.md",
    module="wowlib.formats.adt.chunks",
    stub="wowlib/formats/adt/chunks.pyi",
    headers=(ADT_SRC / "chunks/header.hpp", ADT_SRC / "chunks/texture.hpp",
             ADT_SRC / "chunks/liquid.hpp", ADT_SRC / "chunks/object.hpp"),
)

# The structured liquid + cell records welded at the adt module level (MH2OData,
# LiquidInstance, MapChunkLiquid, MCLQData) carry uint16/uint8 wire fields too.
LIQUID = fr.StructPage(
    page="python/adt/entity.md",
    module="wowlib.formats.adt",
    stub="wowlib/formats/adt/__init__.pyi",
    headers=(ADT_SRC / "liquid.hpp",),
    names_filter=lambda n: n in {"LiquidInstance", "MapChunkLiquid", "MH2OData",
                                 "MCLQData"},
)

FORMAT = fr.Format(
    key="adt",
    fields_page="",            # entity/cell fields render via plain mkdocstrings
    legend_marker="",
    sides=(),
    wowdev_page="ADT/v18",
    anchors_file="adt_wowdev_anchors.json",
    name_re=r"(?!)",           # matches nothing; the genericize passes no-op
    generic_pages=frozenset(),
    forver={},
    struct_pages=(CHUNKS, LIQUID),
)
