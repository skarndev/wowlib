"""ClientDB config for the format-reference engine (format_reference_impl.py).

The ~1200 generated table families (wowlib.db.tables) all share one shape —
a version-agnostic base with ``for_version`` (``Map``), per-era-range record
classes (``MapRecordWotlk``) and per-era-range table classes (``MapWotlk``) —
so the docs render ONE representative family, ``Map``, on
``python/db/tables.md``:

* the ``Map`` base renders via mkdocstrings and gets the injected generic
  ``for_version`` entry (``forver``);
* one concrete table class renders fully (the version-invariant Table API:
  ``records``/``read``/``write``/``strings``/``encrypted_sections``), shown
  generically as ``Map⟨version⟩``;
* the ``MapRecord*`` family renders through the deduplicated record listing
  (``dedup_marker``) — one merged member walk, each member badged with the
  era range that declares it, exactly like the M2 records page.

No sides (record layouts come from WoWDBDefs, not chunk annotations) and no
FourCC badges (databases are not chunked); the anchors file does not exist and
fail-safes to an empty map.
"""

from __future__ import annotations

import format_reference_impl as fr

_MODULE = "wowlib.db.tables"
_STUB = "wowlib/db/tables.pyi"


def _map_record(name: str) -> bool:
    split = fr.split_range_suffix(name)
    return bool(split) and split[0] == "MapRecord"


RECORDS = fr.StructPage(
    page="python/db/tables.md",
    module=_MODULE,
    stub=_STUB,
    names_filter=_map_record,
    dedup_marker="<!-- db-map-records -->",
)

FORMAT = fr.Format(
    key="db",
    fields_page="",
    legend_marker="",
    sides=(),
    wowdev_page="DB2",
    anchors_file="db_wowdev_anchors.json",  # absent on purpose -> no anchors
    name_re=r"Map(?:Record)?",
    generic_pages=frozenset({"python/db/tables.md"}),
    forver={
        "python/db/tables.md": (
            ("wowlib.db.tables.Map", "Map", "Wotlk"),
        ),
    },
    struct_pages=(RECORDS,),
    elem_links={
        "Map": ("python/db/tables.md", _MODULE),
    },
)
