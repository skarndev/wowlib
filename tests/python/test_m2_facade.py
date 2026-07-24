"""The M2 versioned-format facade (formats/m2.cpp): the five family bases
(M2, M2Data, Skin, M2File, Skeleton) with for_version + native inheritance,
the era-subset behavior of the satellite families (Skin is WotLK+, M2File and
Skeleton Legion+), the record classes under the per-family submodules
(formats.m2.body.records, formats.m2.skin), and a synthetic body round-trip
through read()/write() bytes."""

import types

import pytest

import wowlib
from wowlib.formats import m2 as m2_mod
from wowlib.formats.m2 import body as body_mod
from wowlib.formats.m2 import bone as bone_mod
from wowlib.formats.m2 import skin as skin_mod

records_mod = body_mod.records

X = wowlib.Expansion


@pytest.fixture
def fresh_m2():
    return m2_mod.M2.for_version(X.Wotlk)


def test_for_version_returns_the_concrete_class(fresh_m2):
    assert type(fresh_m2).__name__ == "M2Wotlk"


def test_concrete_natively_inherits_the_base(fresh_m2):
    assert m2_mod.M2Wotlk.__bases__ == (m2_mod.M2,)
    assert isinstance(fresh_m2, m2_mod.M2)


def test_submodules_mirror_the_cpp_namespaces():
    assert body_mod.__name__ == "wowlib.formats.m2.body"
    assert skin_mod.__name__ == "wowlib.formats.m2.skin"
    assert bone_mod.__name__ == "wowlib.formats.m2.bone"
    assert records_mod.__name__ == "wowlib.formats.m2.body.records"
    assert hasattr(bone_mod, "BoneFile")


def test_body_defaults_carry_the_wire_versions():
    assert body_mod.M2Data.for_version(X.Vanilla).format_version == 256
    assert body_mod.M2Data.for_version(X.Wotlk).format_version == 264
    assert body_mod.M2Data.for_version(X.Shadowlands).format_version == 274


def test_version_gated_members_do_not_exist_off_era():
    vanilla = body_mod.M2Data.for_version(X.Vanilla)
    wotlk = body_mod.M2Data.for_version(X.Wotlk)
    assert hasattr(vanilla, "skin_profiles")
    assert not hasattr(vanilla, "num_skin_profiles")
    assert hasattr(wotlk, "num_skin_profiles")
    assert not hasattr(wotlk, "skin_profiles")


@pytest.mark.parametrize("module, family, first", [
    ("skin", "Skin", X.Wotlk),
    ("body", "M2File", X.Legion),
    ("m2", "Skeleton", X.Legion),
])
def test_subset_families_reject_earlier_eras(module, family, first):
    mod = {"m2": m2_mod, "body": body_mod, "skin": skin_mod}[module]
    base = getattr(mod, family)
    assert isinstance(base.for_version(first), base)
    with pytest.raises(ValueError):
        base.for_version(X.Vanilla)


@pytest.mark.parametrize("module, alias, count", [
    ("m2", "AnyM2", 11),
    ("body", "AnyM2Data", 11),
    ("skin", "AnySkin", 9),
    ("body", "AnyM2File", 5),
    ("m2", "AnySkeleton", 5),
])
def test_anyx_unions_fold_only_existing_eras(module, alias, count):
    mod = {"m2": m2_mod, "body": body_mod, "skin": skin_mod}[module]
    union = getattr(mod, alias)
    assert isinstance(union, types.UnionType)
    assert len(union.__args__) == count


def test_records_bind_under_their_submodule():
    bone = records_mod.M2CompBoneWotlk()
    assert bone.parent_bone == -1
    seq = records_mod.M2SequenceWotlk()
    seq.duration = 2000
    assert seq.duration == 2000
    profile = skin_mod.M2SkinProfileWotlk()
    assert profile.bone_count_max == 0
    header = m2_mod.SkelHeaderLegion()
    assert header.flags == 0x100


def test_tracks_expose_per_sequence_arrays():
    track = records_mod.M2TrackC3VectorWotlk()
    assert track.global_sequence == 0xFFFF
    old = records_mod.M2TrackC3VectorVanilla()
    assert hasattr(old, "interpolation_ranges")
    assert not hasattr(track, "interpolation_ranges")


def test_synthetic_body_roundtrips_through_bytes():
    model = body_mod.M2Data.for_version(X.Wotlk)
    model.name = "python_test"
    seq = records_mod.M2SequenceWotlk()
    seq.duration = 1000
    seq.flags = 0x20  # data in .m2 — everything inline
    model.sequences.append(seq)
    material = records_mod.M2Material()
    material.blending_mode = 1
    model.materials.append(material)

    blob = model.write()
    back = body_mod.M2Data.for_version(X.Wotlk)
    back.read(blob)
    assert back.name == "python_test"
    assert len(back.sequences) == 1
    assert back.sequences[0].duration == 1000
    assert back.materials[0].blending_mode == 1
    assert back == model


def test_convert_identity_copies(fresh_m2):
    assert type(fresh_m2.convert(X.Wotlk)).__name__ == "M2Wotlk"


def test_convert_stepless_pair_raises(fresh_m2):
    with pytest.raises(wowlib.NotImplemented):
        fresh_m2.convert(X.Shadowlands)


def test_read_write_live_on_the_base_not_the_concrete(fresh_m2):
    assert "read" in m2_mod.M2.__dict__
    assert "read" not in m2_mod.M2Wotlk.__dict__
    assert "read" in m2_mod.Skeleton.__dict__
