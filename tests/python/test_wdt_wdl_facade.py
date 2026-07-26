"""The WDT/WDL versioned-format facades: for_version, native inheritance, the
era-gated satellite/chunk members, the AnyX unions, and the WDL tile-table
round-trip (repeating chunks + MAOF stamping) through the Python surface."""

import types

import pytest

import wowlib
from wowlib.formats import wdl as wdl_mod
from wowlib.formats import wdt as wdt_mod


# --- WDT ---------------------------------------------------------------------


def test_wdt_for_version_returns_the_range_class():
    assert type(wdt_mod.WDT.for_version(wowlib.Expansion.Wotlk)).__name__ == "WDTVanillaToMop"
    assert type(wdt_mod.WDT.for_version(wowlib.Expansion.Shadowlands)).__name__ \
        == "WDTShadowlandsToDragonflight"


def test_wdt_concrete_natively_inherits_the_base():
    w = wdt_mod.WDT.for_version(wowlib.Expansion.Wod)
    assert isinstance(w, wdt_mod.WDT)
    assert wdt_mod.WDTWod.__bases__ == (wdt_mod.WDT,)


def test_wdt_satellites_are_era_gated():
    old = wdt_mod.WDT.for_version(wowlib.Expansion.Mop)
    assert not hasattr(old, "occlusion")
    wod = wdt_mod.WDT.for_version(wowlib.Expansion.Wod)
    assert isinstance(wod.occlusion, wdt_mod.occlusion.WDTOcclusion)
    assert isinstance(wod.lights, wdt_mod.lights.WDTLights)
    assert not hasattr(wod, "fogs")
    new = wdt_mod.WDT.for_version(wowlib.Expansion.Shadowlands)
    assert isinstance(new.fogs, wdt_mod.fogs.WDTFogs)
    assert isinstance(new.particulates, wdt_mod.mpv.WDTParticulates)


def test_wdt_header_family_is_era_split():
    old = wdt_mod.root.WDTRoot.for_version(wowlib.Expansion.Wotlk)
    new = wdt_mod.root.WDTRoot.for_version(wowlib.Expansion.Shadowlands)
    assert hasattr(old.header, "something")
    assert not hasattr(old.header, "occ_fdid")
    assert hasattr(new.header, "occ_fdid")
    assert hasattr(new, "map_anima")  # MANM exists on 9.0+
    assert not hasattr(old, "map_fdids")


@pytest.mark.parametrize("submod, alias", [
    (wdt_mod, "AnyWDT"),
    (wdt_mod.root, "AnyWDTRoot"),
    (wdt_mod.root.chunks, "AnyWDTHeader"),
    (wdt_mod.occlusion, "AnyWDTOcclusion"),
    (wdt_mod.lights, "AnyWDTLights"),
    (wdt_mod.fogs, "AnyWDTFogs"),
    (wdt_mod.mpv, "AnyWDTParticulates"),
    (wdl_mod, "AnyWDL"),
])
def test_anyx_aliases_are_importable(submod, alias):
    # multi-range families fold to a types.UnionType; a single-range family's
    # alias IS its one concrete class (like AnySkeleton)
    value = getattr(submod, alias)
    assert isinstance(value, types.UnionType) or isinstance(value, type)


def test_wdt_root_buffer_roundtrip():
    root = wdt_mod.root.WDTRoot.for_version(wowlib.Expansion.Wotlk)
    root.tiles.resize(64 * 64)
    tile = root.tiles[100]
    tile.flags = 1
    blob = root.write()
    back = wdt_mod.root.WDTRoot.for_version(wowlib.Expansion.Wotlk)
    back.read(blob)
    assert len(back.tiles) == 64 * 64
    assert back.tiles[100].flags == 1
    assert back.write() == blob


def test_wdt_convert_identity_copies():
    w = wdt_mod.WDT.for_version(wowlib.Expansion.Wotlk)
    assert type(w.convert(wowlib.Expansion.Wotlk)).__name__ == "WDTVanillaToMop"


def test_wdt_convert_stepless_pair_raises():
    w = wdt_mod.WDT.for_version(wowlib.Expansion.Wotlk)
    with pytest.raises(wowlib.NotImplemented):
        w.convert(wowlib.Expansion.Shadowlands)


# --- WDL ---------------------------------------------------------------------


def _fresh_wotlk_wdl_with_tiles(slots):
    wdl = wdl_mod.WDL.for_version(wowlib.Expansion.Wotlk)
    wdl.tile_offsets.resize(64 * 64)
    for slot in slots:
        wdl.tile_offsets[slot] = 1
        heights = wdl_mod.chunks.TileHeights()
        heights.outer[0] = slot   # arrays bind by reference: element write-through
        wdl.heightmaps.append(heights)
        wdl.holes.append(wdl_mod.chunks.TileHoles())
    return wdl


def test_wdl_era_gates_its_chunks():
    vanilla = wdl_mod.WDL.for_version(wowlib.Expansion.Vanilla)
    assert not hasattr(vanilla, "holes")
    wotlk = wdl_mod.WDL.for_version(wowlib.Expansion.Wotlk)
    assert hasattr(wotlk, "holes")
    assert not hasattr(wotlk, "lod_doodads")
    sl = wdl_mod.WDL.for_version(wowlib.Expansion.Shadowlands)
    assert hasattr(sl, "lod_doodads")
    assert hasattr(sl, "sky_scenes")
    assert not hasattr(sl, "occlusion_meshes")


def test_wdl_fresh_tile_roundtrip_stamps_maof():
    wdl = _fresh_wotlk_wdl_with_tiles([100, 200, 300])
    blob = wdl.write()
    back = wdl_mod.WDL.for_version(wowlib.Expansion.Wotlk)
    back.read(blob)
    assert len(back.heightmaps) == 3
    assert [h.outer[0] for h in back.heightmaps] == [100, 200, 300]
    assert back.tile_offsets[100] != 0 and back.tile_offsets[200] != 0
    assert back.tile_offsets[99] == 0
    assert back.write() == blob


def test_wdl_pairing_violation_raises():
    wdl = wdl_mod.WDL.for_version(wowlib.Expansion.Wotlk)
    wdl.tile_offsets.resize(64 * 64)
    wdl.tile_offsets[5] = 1  # nonzero slot without a heightmap
    with pytest.raises(wowlib.InvalidEntityState):
        wdl.write()


def test_wdl_fs_verbs_are_reachable_on_the_concrete():
    # the (FileSystem, FileKey) overloads are merged into the concrete's welded
    # read/write chain — both spellings must be visible
    wdl = wdl_mod.WDL.for_version(wowlib.Expansion.Wotlk)
    assert "wowlib.fs.FileSystem" in type(wdl).read.__doc__
    assert "data: bytes" in type(wdl).read.__doc__
    assert "wowlib.fs.FileSystem" in type(wdl).write.__doc__


def test_wdl_ocean_mask_tiles_empty_without_masks():
    wdl = wdl_mod.WDL.for_version(wowlib.Expansion.Shadowlands)
    assert list(wdl.ocean_mask_tiles()) == []
