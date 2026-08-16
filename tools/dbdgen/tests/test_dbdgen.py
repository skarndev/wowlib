"""Unit tests for dbdgen: parser, target matching, member building, range
collapsing and emission — against the vendored Map.dbd / AreaTable.dbd
(tests/data/dbd) plus synthetic snippets."""

from __future__ import annotations

import unittest
from pathlib import Path

from dbdgen import dbd
from dbdgen.emit import (emit_all_tables, Member, build_members, collapse, emit_manifest, emit_table, member_name, range_suffix, snake)
from dbdgen.targets import TARGETS_BY_ERA, locstring_langs

DATA = Path(__file__).resolve().parents[3] / "tests" / "data" / "dbd"

SYNTHETIC = """\
COLUMNS
int ID
int<Map::ID> MapID
locstring Name_lang
string Icon
float Scale
int Flags
int Version

LAYOUT DEADBEEF
BUILD 1.12.1.5875
BUILD 1.11.0.5344-1.12.0.5595
$id$ID<32>
MapID<32>
Name_lang
Icon
Scale
Flags<u32>[2]
Version<u8>

BUILD 3.0.1.8622-3.3.5.12340
$id$ID<32>
Name_lang
"""


class ParserTest(unittest.TestCase):
    def test_synthetic_round(self):
        defn = dbd.parse(SYNTHETIC)
        self.assertEqual(set(defn.columns), {"ID", "MapID", "Name_lang", "Icon",
                                             "Scale", "Flags", "Version"})
        self.assertEqual(defn.columns["MapID"].foreign, "Map::ID")
        self.assertEqual(len(defn.blocks), 2)

        block = defn.block_for_build((1, 12, 1, 5875))
        self.assertIsNotNone(block)
        self.assertEqual(block.layouts, ["DEADBEEF"])
        self.assertTrue(block.matches_build((1, 11, 0, 5400)))
        self.assertFalse(block.matches_build((2, 4, 3, 8606)))

        entries = {e.name: e for e in block.entries}
        self.assertTrue(entries["ID"].is_id)
        self.assertEqual(entries["Flags"].bits, 32)
        self.assertTrue(entries["Flags"].unsigned)
        self.assertEqual(entries["Flags"].array_len, 2)
        self.assertEqual(entries["Version"].bits, 8)

    def test_vendored_map(self):
        defn = dbd.parse((DATA / "Map.dbd").read_text(encoding="utf-8-sig"))
        for era in ("vanilla", "tbc", "wotlk"):
            build = TARGETS_BY_ERA[era].version
            self.assertIsNotNone(defn.block_for_build(build), era)
        vanilla = defn.block_for_build((1, 12, 1, 5875))
        self.assertEqual(vanilla.entries[0].name, "ID")
        self.assertTrue(vanilla.entries[0].is_id)

    def test_noninline_annotations(self):
        defn = dbd.parse((DATA / "Map.dbd").read_text(encoding="utf-8-sig"))
        classic = defn.block_for_build((1, 13, 2, 30073))
        self.assertIsNotNone(classic)
        self.assertTrue(classic.entries[0].noninline)
        self.assertTrue(classic.entries[0].is_id)


class NamingTest(unittest.TestCase):
    def test_snake(self):
        self.assertEqual(snake("MapName"), "map_name")
        self.assertEqual(snake("AreaID"), "area_id")
        self.assertEqual(snake("WdtFileDataID"), "wdt_file_data_id")
        self.assertEqual(snake("ID"), "id")
        self.assertEqual(snake("Field_1_5_0_4442_014"), "field_1_5_0_4442_014")

    def test_member_name(self):
        self.assertEqual(member_name("MapName_lang"), "map_name")
        self.assertEqual(member_name("Version"), "version_")  # record static shadow
        self.assertEqual(member_name("Default"), "default_")  # C++ keyword


class EmitTest(unittest.TestCase):
    def _ranges(self, text=SYNTHETIC, eras=("vanilla", "tbc", "wotlk")):
        defn = dbd.parse(text)
        per_target = []
        for era in eras:
            target = TARGETS_BY_ERA[era]
            block = defn.block_for_build(target.version)
            if block is not None:
                per_target.append((target, build_members(defn, block, target)))
        return collapse(per_target)

    def test_members_and_locstring_width(self):
        ranges = self._ranges()
        # tbc has no block; vanilla and wotlk blocks differ -> two ranges
        self.assertEqual(len(ranges), 2)
        vanilla = {m.name: m for m in ranges[0].members}
        self.assertEqual(vanilla["name"].cpp_type, "LocString8")
        self.assertEqual(vanilla["map_id"].cpp_type, "std::int32_t")
        self.assertEqual(vanilla["flags"].array_len, 2)
        self.assertEqual(vanilla["version_"].cpp_type, "std::uint8_t")
        self.assertIn("References Map::ID.", vanilla["map_id"].doc)

        wotlk = {m.name: m for m in ranges[1].members}
        self.assertEqual(wotlk["name"].cpp_type, "LocString16")

    def test_collapse_identical(self):
        text = SYNTHETIC.replace("BUILD 3.0.1.8622-3.3.5.12340",
                                 "BUILD 2.0.1.6180-3.3.5.12340")
        defn = dbd.parse(text)
        per_target = []
        for era in ("tbc", "wotlk"):
            target = TARGETS_BY_ERA[era]
            per_target.append((target, build_members(defn, defn.block_for_build(target.version), target)))
        ranges = collapse(per_target)
        self.assertEqual(len(ranges), 1)  # identical LocString16 schema collapses
        self.assertEqual([t.era for t in ranges[0].targets], ["tbc", "wotlk"])

    def test_emit_table_shape(self):
        header = emit_table("Widget", self._ranges())
        # Each record specialization carries its own weld (so its per-range alias
        # participates in the welder walk) besides inheriting the row supertype.
        self.assertIn("WidgetRecord<versions::vanilla> : db::rowbase::Widget", header)
        self.assertIn("WidgetRecord<versions::wotlk> : db::rowbase::Widget", header)
        self.assertNotIn("versions::tbc>", header)  # no tbc block
        self.assertIn("[[=db::id]]\n      std::int32_t id = 0;", header)
        self.assertIn("std::array<std::uint32_t, 2> flags{};", header)
        # grid/pivots live in detail (kept off the db.tables module surface).
        self.assertIn("inline constexpr std::array<ClientVersion, 2> "
                      "widget_grid{versions::vanilla, versions::wotlk};", header)
        self.assertIn("widget_pivots{versions::wotlk};", header)
        self.assertIn("detail::widget_pivots, detail::widget_grid", header)
        # A ranges table checked against C++ range_suffix guards naming drift.
        self.assertIn("static_assert(formats::ranges_valid(widget_ranges, "
                      "widget_pivots, widget_grid));", header)
        # Welded bases + wrapper for the Python/Lua binding surface.
        self.assertIn("=welder::weld,", header)
        self.assertIn("namespace wowlib::db::rowbase", header)
        self.assertIn("=welder::weld_as(\"Widget\"),", header)
        # The wrapper too carries its own weld besides inheriting the table supertype.
        self.assertIn("Widget_ : TableBase", header)
        self.assertIn("WidgetTable : Widget_", header)
        # the method surface is INHERITED — the class contributes records + wiring
        self.assertIn("std::vector<WidgetRecord<V>> records;", header)
        self.assertIn("mark::exclude(welder::lang::py)", header)
        self.assertIn("core_.wire(&records", header)
        self.assertIn("using Widget = detail::WidgetTable<formats::canonical_version("
                      "V, detail::widget_pivots, detail::widget_grid)>;", header)

    _NOLOC = """\
COLUMNS
int ID
int Value

BUILD 1.12.1.5875
BUILD 2.4.3.8606
$id$ID<32>
Value<32>

BUILD 3.3.5.12340
$id$ID<32>
"""

    def test_range_suffix(self):
        # Distinct single-era ranges -> plain Expansion spellings (SYNTHETIC has
        # no tbc block, so its grid is vanilla, wotlk).
        ranges = self._ranges()
        grid = [t for r in ranges for t in r.targets]
        self.assertEqual([range_suffix(r, grid) for r in ranges],
                         ["Vanilla", "Wotlk"])
        # A range reaching the grid's end -> "<first>Plus".
        text = SYNTHETIC.replace("BUILD 3.0.1.8622-3.3.5.12340",
                                 "BUILD 2.0.1.6180-3.3.5.12340")
        plus = self._ranges(text, eras=("tbc", "wotlk"))
        pgrid = [t for r in plus for t in r.targets]
        self.assertEqual([range_suffix(r, pgrid) for r in plus], ["TbcPlus"])
        # An interior multi-era range -> "<first>To<last>".
        interior = self._ranges(self._NOLOC, eras=("vanilla", "tbc", "wotlk"))
        igrid = [t for r in interior for t in r.targets]
        self.assertEqual([range_suffix(r, igrid) for r in interior],
                         ["VanillaToTbc", "Wotlk"])

    def test_emit_manifest_shape(self):
        manifest = emit_manifest("wotlk", ["AreaTable", "Map"])
        self.assertIn("#include <wowlib/db/tables/area_table.hpp>", manifest)
        self.assertIn("#define WOWLIB_DB_TABLES_WOTLK(X) \\\n  X(AreaTable) \\\n  X(Map)",
                      manifest)

    def test_emit_all_tables_shape(self):
        umbrella = emit_all_tables(["AreaTable", "Map"])
        self.assertIn("#pragma once", umbrella)
        self.assertIn("#include <wowlib/db/tables/area_table.hpp>", umbrella)
        self.assertIn("#include <wowlib/db/tables/map.hpp>", umbrella)
        # language-neutral: nothing but includes, so any backend can use it
        body = [l for l in umbrella.splitlines()
                if l and not l.startswith(("//", "#pragma"))]
        self.assertTrue(all(l.startswith("#include ") for l in body), body)

    def test_lang_collision_keeps_suffix(self):
        text = """\
COLUMNS
int ID
string Name
locstring Name_lang

BUILD 1.12.1.5875
$id$ID<32>
Name
Name_lang
"""
        defn = dbd.parse(text)
        target = TARGETS_BY_ERA["vanilla"]
        members = build_members(defn, defn.block_for_build(target.version), target)
        self.assertEqual([m.name for m in members], ["id", "name", "name_lang"])

    def test_locstring_langs(self):
        self.assertEqual(locstring_langs((1, 12, 1, 5875)), 8)
        self.assertEqual(locstring_langs((2, 0, 3, 6299)), 8)   # pre-2.1: no ruRU yet
        self.assertEqual(locstring_langs((2, 4, 3, 8606)), 16)
        self.assertEqual(locstring_langs((3, 3, 5, 12340)), 16)
        self.assertIsNone(locstring_langs((4, 3, 4, 15595)))


if __name__ == "__main__":
    unittest.main()
