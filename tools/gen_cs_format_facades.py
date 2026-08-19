"""Generate the C# per-era format factories (FormatFacades.cs).

The C# surface welds one concrete class per version RANGE
(``Formats.Adt.ADTWotlk``, ``Formats.Wmo.WMOVanillaToWod``, ...). The
version-agnostic DATA surface is the rod's: welder-csharp's family synthesis
(options::family_surface) hoists the member intersection onto each welded
family base as dispatch members — fs verbs included — so a base-typed
instance reads, writes and carries data without a downcast. What the rod
CANNOT know is the era ladder: which range covers which expansion. That
mapping is this script's contribution — one static factory per welded family
with one method per era, each returning that era's covering range class,
plus the dynamic ``ForVersion``::

    var adt = Formats.Adt.ADT.Wotlk();     // -> ADTWotlk (typed)
    var wmo = Formats.Wmo.WMO.ForVersion(fs.Version);  // -> the family base

The era -> range mapping is read from the X-macro tables in
``bindings/instantiations/*_ranges.hpp`` — the same single source of truth
the welded aliases and the generator contributors use. Each X row names a
range's SUFFIX and its FIRST era; a range covers from its first era up to
the next row's.

    python tools/gen_cs_format_facades.py <bindings/instantiations> <out.cs>
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# The era order (targets.py / core/client_version.hpp) and the PascalCase
# method spellings (the Expansion enumerator names the class suffixes use).
ERAS = [
    ("vanilla", "Vanilla"),
    ("tbc", "Tbc"),
    ("wotlk", "Wotlk"),
    ("cata", "Cata"),
    ("mop", "Mop"),
    ("wod", "Wod"),
    ("legion", "Legion"),
    ("bfa", "Bfa"),
    ("shadowlands", "Shadowlands"),
    ("dragonflight", "Dragonflight"),
    ("tww", "TheWarWithin"),
]
ERA_INDEX = {era: i for i, (era, _) in enumerate(ERAS)}

# (family alias, C# namespace) per RANGES macro — mirrors the contributor
# TUs (bindings/csharp/gen_<fmt>.cpp); extend both when a family is added.
FAMILIES: dict[str, list[tuple[str, str]]] = {
    "WOWLIB_WMO_RANGES_ROOT": [("WMORoot", "Formats.Wmo.Root")],
    "WOWLIB_WMO_RANGES_GROUP": [("WMOGroupBody", "Formats.Wmo.Group"),
                                ("WMOGroup", "Formats.Wmo.Group")],
    "WOWLIB_WMO_RANGES_GROUP_HEADER": [("WMOGroupHeader",
                                        "Formats.Wmo.Group.Chunks")],
    "WOWLIB_WMO_RANGES_BATCH": [("WMOBatch", "Formats.Wmo.Group.Chunks")],
    "WOWLIB_WMO_RANGES_ASSEMBLY": [("WMO", "Formats.Wmo")],
    "WOWLIB_M2_RANGES_TRACKS": [
        (name, "Formats.M2.Root.Record") for name in (
            "M2TrackC3Vector", "M2TrackC4Quaternion", "M2TrackCompQuat",
            "M2TrackFloat", "M2TrackFixed16", "M2TrackUInt8", "M2TrackUInt16",
            "M2TrackSplineC3Vector", "M2TrackSplineFloat", "M2EventTrack",
            "M2Color", "M2TextureWeight", "M2TextureFlipbook",
            "M2TextureTransform", "M2Attachment", "M2Event", "M2Light",
            "M2Ribbon")],
    "WOWLIB_M2_RANGES_SEQUENCE": [("M2Sequence", "Formats.M2.Root.Record")],
    "WOWLIB_M2_RANGES_BONE": [("M2CompBone", "Formats.M2.Root.Record")],
    "WOWLIB_M2_RANGES_CAMERA": [("M2Camera", "Formats.M2.Root.Record")],
    "WOWLIB_M2_RANGES_PARTICLE": [("M2Particle", "Formats.M2.Root.Record")],
    "WOWLIB_M2_RANGES_DATA": [("M2Root", "Formats.M2.Root")],
    "WOWLIB_M2_RANGES_FILE": [("M2ChunkedFile", "Formats.M2.Chunked")],
    "WOWLIB_M2_RANGES_SKIN_SECTION": [("M2SkinSection", "Formats.M2.Skin")],
    "WOWLIB_M2_RANGES_SKIN_PROFILE": [("M2SkinProfile", "Formats.M2.Skin")],
    "WOWLIB_M2_RANGES_SKIN": [("Skin", "Formats.M2.Skin")],
    "WOWLIB_M2_RANGES_CHUNK_PAYLOADS": [
        ("SkelHeader", "Formats.M2"), ("SkelSequences", "Formats.M2"),
        ("SkelBones", "Formats.M2"), ("SkelAttachments", "Formats.M2"),
        ("Skeleton", "Formats.M2")],
    "WOWLIB_M2_RANGES_ASSEMBLY": [("M2", "Formats.M2")],
    "WOWLIB_ADT_RANGES_ASSEMBLY": [("ADT", "Formats.Adt")],
    "WOWLIB_ADT_RANGES_MAPCHUNK": [("MapChunk", "Formats.Adt")],
    "WOWLIB_WDT_RANGES_ROOT": [("WDTRoot", "Formats.Wdt.Root")],
    "WOWLIB_WDT_RANGES_HEADER": [("WDTHeader", "Formats.Wdt.Root.Chunks")],
    "WOWLIB_WDT_RANGES_OCCLUSION": [("WDTOcclusion", "Formats.Wdt.Occlusion")],
    "WOWLIB_WDT_RANGES_LIGHTS": [("WDTLights", "Formats.Wdt.Lights")],
    "WOWLIB_WDT_RANGES_FOGS": [("WDTFogs", "Formats.Wdt.Fogs")],
    "WOWLIB_WDT_RANGES_MPV": [("WDTParticulates", "Formats.Wdt.Mpv")],
    "WOWLIB_WDT_RANGES_ASSEMBLY": [("WDT", "Formats.Wdt")],
    "WOWLIB_WDL_RANGES": [("WDL", "Formats.Wdl")],
}


# Families whose concretes DERIVE a welded family base in the wrapper —
# these get a dynamic ForVersion(Expansion) returning the base, on top of
# the per-era statics. (The M2 record families weld standalone concretes.)
BASED = {
    "WMORoot", "WMOGroupBody", "WMOGroup", "WMOGroupHeader", "WMOBatch",
    "WMO", "M2Root", "M2ChunkedFile",
    "Skin", "Skeleton", "M2", "ADT", "MapChunk", "WDTRoot", "WDTHeader",
    "WDTOcclusion", "WDTLights", "WDTFogs", "WDTParticulates", "WDT", "WDL",
}


def ranges_of(text: str, macro: str) -> list[tuple[str, str]]:
    """The (suffix, first_era) rows of one X-macro table."""
    block = re.search(rf"#define {macro}\(X\)(.*?)\n\n", text, re.S)
    if not block:
        raise SystemExit(f"gen_cs_format_facades: {macro} not found")
    return re.findall(r"X\((\w+), (\w+)\)", block.group(1))


def main() -> int:
    instantiations, out_path = Path(sys.argv[1]), Path(sys.argv[2])
    texts = {p.stem: p.read_text(encoding="utf-8")
             for p in instantiations.glob("*_ranges.hpp")}
    joined = "\n\n".join(texts.values())

    by_namespace: dict[str, list[str]] = {}
    for macro, families in FAMILIES.items():
        rows = ranges_of(joined, macro)
        # Era -> covering range: the row with the largest first-era <= era.
        starts = [(ERA_INDEX[era], suffix) for suffix, era in rows]
        for family, namespace in families:
            lines = by_namespace.setdefault(namespace, [])
            covering_by_era: list[tuple[str, str, str]] = []
            for index, (era, method) in enumerate(ERAS):
                covering = None
                for start, suffix in starts:
                    if start <= index:
                        covering = suffix
                if covering is not None:
                    covering_by_era.append((era, method, covering))
            lines.append(f"    /// <summary>Era-resolved construction for the"
                         f" {family} family (the C# for_version): one static"
                         f" per era returning the concrete range class, plus"
                         f" ForVersion(Expansion) and ForVersion(ClientVersion)"
                         f" where a common welded base exists.</summary>")
            lines.append(f"    public partial class {family}")
            lines.append("    {")
            if family in BASED:
                lines.append(f"        /// <summary>A fresh {family} for the"
                             f" given era, as the family base — the dynamic"
                             f" twin of the typed per-era statics.</summary>")
                lines.append(f"        public static {family} ForVersion("
                             f"wowlib.Expansion era) => era switch")
                lines.append("        {")
                for era, method, covering in covering_by_era:
                    lines.append(f"            wowlib.Expansion.{method} => "
                                 f"new {family}{covering}(),")
                lines.append("            _ => throw new System.ArgumentOut"
                             "OfRangeException(nameof(era)),")
                lines.append("        };")
                lines.append("")
                lines.append(f"        /// <summary>A fresh {family} for an"
                             f" exact client version — the only axis that can"
                             f" name a Classic client, whose files follow the"
                             f" engine its build was cut on rather than its"
                             f" 1.15 / 4.4 version number.</summary>")
                lines.append(f"        public static {family} ForVersion("
                             f"wowlib.ClientVersion version) => ForVersion(")
                lines.append("            wowlib.Global.ExpansionOf(version"
                             ".FormatLineage)")
                lines.append("            ?? throw new System.ArgumentOutOf"
                             "RangeException(nameof(version)));")
            for era, method, covering in covering_by_era:
                lines.append(f"        /// <summary>A fresh {family} for"
                             f" {era} clients ({family}{covering}).</summary>")
                lines.append(f"        public static {family}{covering} "
                             f"{method}() => new {family}{covering}();")
            lines.append("    }")
            lines.append("")

    out = ["// <auto-generated> per-era format factories over the welded",
           "// range classes (tools/gen_cs_format_facades.py). Do not edit.",
           "#nullable enable", ""]
    for namespace, lines in by_namespace.items():
        out.append(f"namespace wowlib.{namespace}")
        out.append("{")
        out.extend(lines)
        out.append("}")
        out.append("")
    text = "\n".join(out)
    if not out_path.exists() or out_path.read_text(encoding="utf-8") != text:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(text, encoding="utf-8")
    print(f"gen_cs_format_facades: {sum(len(f) for f in FAMILIES.values())} "
          f"families -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
