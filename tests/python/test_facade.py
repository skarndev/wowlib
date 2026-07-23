"""The versioned-format facade (facade.hpp + formats/wmo.cpp): a welded empty C++
base per version-differing family gives REAL native inheritance and isinstance (no
ABC glue), a for_version() factory, a convert() method, and importable runtime AnyX
union aliases. This dispatch machinery is entirely hand-built on top of welder."""

import types

import pytest

import wowlib
from wowlib.formats import wmo as wmo_mod

root_mod = wmo_mod.root
group_mod = wmo_mod.group
gchunks_mod = wmo_mod.group.chunks


def test_for_version_returns_the_concrete_class(fresh_wmo):
    assert type(fresh_wmo).__name__ == "WMOWotlk"


def test_concrete_natively_inherits_the_base(fresh_wmo):
    assert wmo_mod.WMOWotlk.__bases__ == (wmo_mod.WMO,)
    assert isinstance(fresh_wmo, wmo_mod.WMO)
    assert issubclass(wmo_mod.WMOWotlk, wmo_mod.WMO)


def test_families_do_not_cross_inherit(fresh_wmo):
    assert not isinstance(fresh_wmo, root_mod.WMORoot)


def test_wire_struct_family_has_a_facade_too():
    header = gchunks_mod.WMOGroupHeader.for_version(wowlib.Expansion.Shadowlands)
    assert isinstance(header, gchunks_mod.WMOGroupHeader)


@pytest.mark.parametrize("submod, alias", [
    (wmo_mod, "AnyWMO"),
    (root_mod, "AnyWMORoot"),
    (group_mod, "AnyWMOGroup"),
    (group_mod, "AnyWMOGroupBody"),
    (gchunks_mod, "AnyWMOGroupHeader"),
    (gchunks_mod, "AnyWMOBatch"),
])
def test_anyx_unions_are_importable_runtime_uniontypes(submod, alias):
    assert isinstance(getattr(submod, alias), types.UnionType)


def test_concrete_isinstance_matches_the_union(fresh_wmo):
    assert isinstance(fresh_wmo, wmo_mod.AnyWMO)


def test_convert_identity_copies(fresh_wmo):
    assert type(fresh_wmo.convert(wowlib.Expansion.Wotlk)).__name__ == "WMOWotlk"


def test_convert_stepless_pair_raises(fresh_wmo):
    with pytest.raises(wowlib.NotImplemented):
        fresh_wmo.convert(wowlib.Expansion.Shadowlands)


def test_fresh_entity_carries_wire_defaults(fresh_wmo):
    assert fresh_wmo.root.mver == 17


def test_roundtrip_internals_stay_hidden(fresh_wmo):
    assert not hasattr(fresh_wmo.root, "journal")  # ChunkExtras internals unwelded


def test_read_write_live_on_the_base_not_the_concrete(fresh_wmo):
    assert all(hasattr(fresh_wmo, verb) for verb in ("read", "write", "convert"))
    assert "read" not in wmo_mod.WMOWotlk.__dict__
    assert hasattr(fresh_wmo.root, "read") and hasattr(fresh_wmo.root, "write")
