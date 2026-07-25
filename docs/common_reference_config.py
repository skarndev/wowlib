"""Shared-primitive int-width config for the format-reference engine.

The common math/colour primitives (`wowlib.formats.common`) and the core
version/id/error types (`wowlib`) render on plain mkdocstrings pages with no
version axis, so — unlike WMO/M2 — they need none of the badge/dedup/genericize
machinery, only the wire int-width annotation nanobind erases (uint8 -> int).
This module registers them as width-only StructPages under a minimal Format:
its empty sides / generic_pages / forver make every engine pass but the
int-width one a no-op for these pages.
"""

from __future__ import annotations

import format_reference_impl as fr

SRC = fr.REPO_ROOT / "src/wowlib"

# The shared wire math/colour structs (CArgb, C3iVector, fixed16, …). Float-only
# structs (C3Vector, matrices) simply carry no fixed-width int members, so the
# engine skips them; no filter needed.
COMMON_TYPES = fr.StructPage(
    page="python/common.md",
    module="wowlib.formats.common",
    stub="wowlib/formats/common.pyi",
    headers=(SRC / "formats/common/types.hpp",),
)
# Not covered (accepted bare): StringBlock/ChunkBlob live in module
# `wowlib.formats` and their only ints are the size() getters (size_t, not wire)
# plus the NESTED StringBlock.Entry.offset — a nested class path the flat
# module.class.field width matcher does not reach. One minor field; left as-is.

# The core value types on the top-level `wowlib` module. Restricted to the three
# with wire fields — the module also hosts every opaque Vector wrapper and the
# welded format bases, which have no fixed-width members to annotate.
CORE_TYPES = fr.StructPage(
    page="python/core.md",
    module="wowlib",
    stub="wowlib/__init__.pyi",
    headers=(SRC / "core/client_version.hpp", SRC / "core/file_key.hpp",
             SRC / "core/error.hpp"),
    names_filter=lambda n: n in {"ClientVersion", "FileDataID", "Error"},
)

FORMAT = fr.Format(
    key="common",
    fields_page="",            # no field surfaces
    legend_marker="",
    sides=(),
    wowdev_page="Common_Types",
    anchors_file="",           # absent file -> {} (fail-safe in Format.anchors)
    name_re=r"(?!)",           # matches nothing; res() is never reached anyway
    generic_pages=frozenset(),
    forver={},
    struct_pages=(COMMON_TYPES, CORE_TYPES),
)
