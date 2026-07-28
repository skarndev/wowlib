"""The ADT versioned-format facade: for_version, native inheritance, the
era-gated tile/cell members, the AnyX unions, opaque cell containers, and a
real-client read + semantic buffer round-trip through the Python surface."""

import types

import pytest

import wowlib
from wowlib.formats import adt as adt_mod


# --- facade shape ------------------------------------------------------------


def test_adt_for_version_returns_the_range_class():
    assert type(adt_mod.ADT.for_version(wowlib.Expansion.Wotlk)).__name__ == "ADTWotlk"
    assert type(adt_mod.ADT.for_version(wowlib.Expansion.Shadowlands)).__name__ == "ADTBfaPlus"
    assert type(adt_mod.ADT.for_version(wowlib.Expansion.Cata)).__name__ == "ADTCataToLegion"


def test_adt_concrete_natively_inherits_the_base():
    a = adt_mod.ADT.for_version(wowlib.Expansion.Wotlk)
    assert isinstance(a, adt_mod.ADT)
    assert adt_mod.ADTWotlk.__bases__ == (adt_mod.ADT,)


def test_adt_tile_members_are_era_gated():
    vanilla = adt_mod.ADT.for_version(wowlib.Expansion.Vanilla)
    assert not hasattr(vanilla, "flying_bounds")  # MFBO is BC+
    assert not hasattr(vanilla, "water")          # MH2O is WotLK+
    wotlk = adt_mod.ADT.for_version(wowlib.Expansion.Wotlk)
    assert hasattr(wotlk, "flying_bounds")
    assert isinstance(wotlk.water, adt_mod.MH2OData)
    assert not hasattr(wotlk, "mamp")             # split-file, Cata+
    sl = adt_mod.ADT.for_version(wowlib.Expansion.Shadowlands)
    assert hasattr(sl, "mamp")
    assert hasattr(sl, "diffuse_texture_ids")     # MDID, 8.1+
    assert not hasattr(wotlk, "diffuse_texture_ids")


def test_mapchunk_cell_members_are_era_gated():
    vanilla = adt_mod.MapChunk.for_version(wowlib.Expansion.Vanilla)
    assert hasattr(vanilla, "legacy_liquid")      # MCLQ, up to and including WotLK
    assert not hasattr(vanilla, "vertex_colors")  # MCCV, WotLK+
    wotlk = adt_mod.MapChunk.for_version(wowlib.Expansion.Wotlk)
    assert hasattr(wotlk, "vertex_colors")
    assert hasattr(wotlk, "legacy_liquid")        # MCLQ survives WotLK (Outland tiles)
    assert not hasattr(wotlk, "vertex_lighting")  # MCLV, Cata+
    cata = adt_mod.MapChunk.for_version(wowlib.Expansion.Cata)
    assert hasattr(cata, "vertex_lighting")
    assert hasattr(cata, "material_ids")
    assert not hasattr(cata, "legacy_liquid")     # MCLQ finally gone at Cata


@pytest.mark.parametrize("alias", ["AnyADT", "AnyMapChunk"])
def test_any_unions_are_real_union_objects(alias):
    union = getattr(adt_mod, alias)
    assert isinstance(union, types.UnionType)


def test_adt_chunks_bind_opaque():
    a = adt_mod.ADT.for_version(wowlib.Expansion.Wotlk)
    assert type(a.chunks).__name__ == "VectorMapChunkWotlk"


# --- real-client read (3.3.5a monolithic) ------------------------------------


def test_mono_read_and_structural_invariants(wotlk_fs):
    a = adt_mod.ADT.for_version(wowlib.Expansion.Wotlk)
    # 37_23 was the tile the MCSH size-correction bug first surfaced on. The
    # on-disk alpha bit depth is supplied by the caller (from the map's WDT);
    # wowlib does not resolve it. Azeroth ships 4-bit alpha in 3.3.5a.
    a.read(
        wotlk_fs,
        wowlib.FileKey("World/Maps/Azeroth/Azeroth_37_23.adt"),
        adt_mod.AlphaFormat.lowres_4bit,
    )
    assert len(a.chunks) == 256
    for c in a.chunks:
        assert len(c.heights) in (0, 145)
        assert len(c.normals) in (0, 145)
        assert len(c.shadow_map) in (0, 4096)
        assert len(c.alpha_maps) == len(c.layers)
        assert all(len(m) in (0, 4096) for m in c.alpha_maps)


def test_add_a_texture_is_version_agnostic():
    """The whole point of the unified ADT: adding a texture never branches on
    which physical file it lands in — it is just an append to a member."""
    wotlk = adt_mod.ADT.for_version(wowlib.Expansion.Wotlk)
    wotlk.textures.add("tileset\\generic\\black.blp")
    assert wotlk.textures.entries()[0].value == "tileset\\generic\\black.blp"
    # the same call shape works on a split-file (Cata+) tile
    sl = adt_mod.ADT.for_version(wowlib.Expansion.Shadowlands)
    sl.textures.add("tileset\\generic\\black.blp")
    assert len(sl.textures.entries()) == 1
