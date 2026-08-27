"""The validate()/ensure_valid() binding surface.

Contract correctness itself is Catch2's job on the C++ side (see
tests/unit/test_validation.cpp); what is asserted here is what welder does not
guarantee on its own — that the report type crosses the boundary with a usable
read-only surface, that the findings iterate as value objects, and that
ensure_valid() raises the typed exception rather than returning a status.
"""

import pytest

import wowlib
from wowlib.formats import ValidationReport, ValidationSeverity
from wowlib.formats import blp as blp_mod
from wowlib.formats import wmo as wmo_mod


def test_fresh_report_is_empty_and_ok():
    report = ValidationReport()
    assert report.ok
    assert report.size == 0
    assert report.error_count == 0
    assert report.warning_count == 0
    assert not report.truncated


def test_validate_returns_a_report(fresh_wmo):
    report = fresh_wmo.validate()
    assert isinstance(report, ValidationReport)
    assert report.ok


def test_report_is_read_only():
    """The Python surface is the query half only — mutators and the walker's
    own plumbing stay on the C++ side."""
    report = ValidationReport()
    for absent in ("add", "add_error", "add_warning", "prefix_from", "full", "to_result"):
        assert not hasattr(report, absent), absent


def test_findings_carry_severity_path_and_message():
    # a default BLP has no dimensions and no base level: two errors
    texture = blp_mod.BLP()
    report = texture.validate()

    assert not report.ok
    assert report.error_count == 2
    assert report.size == len(list(report.issues))

    issues = list(report.issues)
    assert all(i.severity == ValidationSeverity.Error for i in issues)
    assert {i.path for i in issues} == {"width", "mips[0]"}
    assert all(i.message for i in issues)


def test_ensure_valid_raises_the_typed_error():
    texture = blp_mod.BLP()
    with pytest.raises(wowlib.InvalidEntityState) as caught:
        texture.ensure_valid()
    # the folded message names the failing members, not just a count
    assert "mips[0]" in str(caught.value)


def test_ensure_valid_passes_on_a_consistent_entity(fresh_wmo):
    assert fresh_wmo.ensure_valid() is None


def test_validate_is_available_across_the_formats():
    """Every format entity carries the pair, whichever engine backs it: the
    chunk mixin (WMORoot), the offset mixin (M2Root), the bespoke entities
    (ADT, BLP) and the assemblies."""
    from wowlib.formats import adt as adt_mod
    from wowlib.formats import m2 as m2_mod
    from wowlib.formats.m2 import root as m2_root_mod
    from wowlib.formats.wmo import root as wmo_root_mod

    wotlk = wowlib.Expansion.Wotlk
    entities = [
        wmo_mod.WMO.for_version(wotlk),
        wmo_root_mod.WMORoot.for_version(wotlk),
        m2_mod.M2.for_version(wotlk),
        m2_root_mod.M2Root.for_version(wotlk),
        adt_mod.ADT.for_version(wotlk),
        blp_mod.BLP(),
    ]
    for entity in entities:
        assert isinstance(entity.validate(), ValidationReport), type(entity).__name__
        assert hasattr(entity, "ensure_valid"), type(entity).__name__


def test_the_verbs_live_on_the_abstract_bases_too():
    """welder binds both on every concrete, which is enough to CALL them; the
    facade also binds them on the family base so code annotated against the
    abstract family type-checks. Calling through the base's unbound method is
    what proves the dispatch actually reaches the concrete."""
    from wowlib.formats import adt as adt_mod
    from wowlib.formats import m2 as m2_mod
    from wowlib.formats import wdl as wdl_mod

    for base in (wmo_mod.WMO, m2_mod.M2, adt_mod.ADT, wdl_mod.WDL):
        assert hasattr(base, "validate"), base.__name__
        assert hasattr(base, "ensure_valid"), base.__name__

    entity = wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)
    assert isinstance(wmo_mod.WMO.validate(entity), ValidationReport)
    assert wmo_mod.WMO.ensure_valid(entity) is None


def test_binary_struct_families_do_not_gain_the_verbs():
    """A base method that could only ever raise is worse than an absent one, so
    families whose concretes have no validate() skip the binding entirely."""
    from wowlib.formats.wmo.group import chunks as group_chunks

    assert not hasattr(group_chunks.WMOBatch, "validate")


def test_dispatching_a_foreign_instance_raises_type_error():
    from wowlib.formats import adt as adt_mod

    with pytest.raises(TypeError):
        wmo_mod.WMO.validate(adt_mod.ADT.for_version(wowlib.Expansion.Wotlk))


# --- real-client end to end ---------------------------------------------------


def test_client_wmo_validates_clean(wotlk_fs):
    """A file read from a client and left unmodified reports no errors — the
    same guarantee the C++ corpus sweeps assert, exercised through Python."""
    wmo = wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)
    wmo.read(
        wotlk_fs,
        wowlib.FileKey("World/wmo/Azeroth/Buildings/Stormwind/Stormwind.wmo"),
    )
    report = wmo.validate()
    assert report.error_count == 0, [
        f"{i.path}: {i.message}" for i in report.issues if i.severity == ValidationSeverity.Error
    ]
    assert wmo.ensure_valid() is None


def test_a_broken_reference_is_reported_with_its_path(wotlk_fs):
    """Break one contract on a real file and the finding names the member."""
    wmo = wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)
    wmo.read(
        wotlk_fs,
        wowlib.FileKey("World/wmo/Azeroth/Buildings/Stormwind/Stormwind.wmo"),
    )
    # MOGI must describe exactly the group files held
    del wmo.root.group_infos[:]

    report = wmo.validate()
    assert not report.ok
    assert any(i.path == "root.groupInfos" for i in report.issues)
    with pytest.raises(wowlib.InvalidEntityState):
        wmo.ensure_valid()
