"""The hand-bound value types: FileDataID, the version-independent FileKey identity
(with its path canonicalization), the immutable FileSystemSettings value, and the
StringBlock. These are custom casters/constructors, not plain welded structs."""

import pytest

import wowlib


def test_filedataid_value():
    assert wowlib.FileDataID(775971).value == 775971


def test_filekey_path_is_canonicalized():
    key = wowlib.FileKey("World/Maps/Azeroth/Azeroth.wdt")
    assert key.path == "world\\maps\\azeroth\\azeroth.wdt"
    assert key.fdid is None  # pre-CASC style key: fdid simply absent


def test_filekey_id_only():
    key = wowlib.FileKey(wowlib.FileDataID(775971))
    assert key.path is None
    assert key.fdid is not None and key.fdid.value == 775971


def test_filekey_both_halves():
    key = wowlib.FileKey("a/b.blp", wowlib.FileDataID(7))
    assert key.path == "a\\b.blp"
    assert key.fdid is not None and key.fdid.value == 7


def test_settings_carry_nsdmi_defaults():
    s = wowlib.fs.FileSystemSettings(client_path="/no/such/client",
                                     version=wowlib.versions.wotlk)
    # casc_product is unset by default: open() derives it from the version's
    # flavor, which is the only way one default can serve retail AND Classic.
    assert s.casc_product is None
    assert s.version.default_casc_product == "wow"
    assert s.locale == wowlib.Locale.enUS
    assert s.custom_fdid_start.value == 1_000_000_000


def test_settings_fields_are_read_only():
    s = wowlib.fs.FileSystemSettings(client_path="/c", version=wowlib.versions.wotlk)
    with pytest.raises(AttributeError):
        s.client_path = "/elsewhere"


def test_settings_keyword_skips_middle_defaults():
    s = wowlib.fs.FileSystemSettings("/c", wowlib.versions.wotlk, casc_product="wowt")
    assert s.casc_product == "wowt"


def test_stringblock_add_at_and_entries():
    block = wowlib.formats.StringBlock()
    offset = block.add("textures/stone.blp")
    assert offset == 0
    assert block.at(offset) == "textures/stone.blp"
    entry = block.entries()[0]
    assert entry.offset == 0 and entry.value == "textures/stone.blp"
