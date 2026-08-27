"""The welded surface: version constants, enums, the expansion mapping, docstrings,
and — importantly — that internal C++ types stay *unwelded*. The minimalism checks
guard against accidental over-exposure when the reflection walk changes."""

import pytest

import wowlib
from wowlib.formats.wmo.group import chunks as gchunks


def test_version_constants():
    assert wowlib.versions.wotlk.build == 12340
    assert wowlib.versions.tww.build == 65299


def test_storage_kind_enum():
    assert wowlib.versions.wotlk.storage_kind == wowlib.StorageKind.Mpq
    assert wowlib.versions.shadowlands.storage_kind == wowlib.StorageKind.Casc


def test_storage_kind_is_read_only():
    with pytest.raises(AttributeError):
        wowlib.versions.wotlk.storage_kind = wowlib.StorageKind.Casc


def test_locale_enum_present():
    assert wowlib.Locale.enUS is not None


def test_expansion_maps_to_version_constant():
    assert wowlib.to_client_version(wowlib.Expansion.Wotlk) == wowlib.versions.wotlk


def test_version_constant_maps_to_expansion():
    assert wowlib.to_expansion(wowlib.versions.shadowlands) == wowlib.Expansion.Shadowlands


def test_flag_enum_exposes_bit_values():
    # GroupFlags is an IntEnum; bit tests work against the plain integer fields.
    assert gchunks.GroupFlags.Exterior == 0x8


@pytest.mark.parametrize("absent", ["ErrorCode"])
def test_internal_module_types_not_welded(absent):
    assert not hasattr(wowlib, absent)


@pytest.mark.parametrize("absent", ["ProjectDirectory", "CsvListfile", "CsvListfileOptions"])
def test_internal_fs_types_not_welded(absent):
    assert not hasattr(wowlib.fs, absent)


def test_docstrings_carried_through_welder():
    assert "FileDataID" in (wowlib.FileDataID.__doc__ or "")
    assert "filesystem" in (wowlib.fs.__doc__ or "").lower()
