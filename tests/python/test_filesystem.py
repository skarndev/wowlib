"""The FileSystem facade (fs.cpp): explicit-lifetime control welded from the
protected C++ surface — is_open, close, the context-manager protocol — plus the
generic FileKey identity and typed error-after-close. The real-storage assertions
opt in via WOWLIB_TEST_CLIENTS_DIR (the wotlk_fs / clients_dir fixtures skip
otherwise); the surface checks always run."""

import wowlib
import pytest


def test_lifetime_surface_is_welded():
    assert hasattr(wowlib.fs.FileSystem, "close")
    assert isinstance(wowlib.fs.FileSystem.is_open, property)
    assert hasattr(wowlib.fs.FileSystem, "__enter__")


def test_read_returns_bytes_and_filekey_is_equivalent(wotlk_fs):
    assert wotlk_fs.kind == wowlib.StorageKind.Mpq
    data = wotlk_fs.read_file("DBFilesClient/Map.dbc")
    assert isinstance(data, bytes) and data[:4] == b"WDBC"
    assert wotlk_fs.read_file(wowlib.FileKey("DBFilesClient/Map.dbc")) == data
    assert wotlk_fs.exists(wowlib.FileKey("DBFilesClient/Map.dbc"))


def test_resolve_canonicalizes_and_leaves_fdid_absent_for_mpq(wotlk_fs):
    resolved = wotlk_fs.resolve(wowlib.FileKey("DBFilesClient/Map.dbc"))
    assert resolved.path == "dbfilesclient\\map.dbc"
    assert resolved.fdid is None


def test_close_is_deterministic_idempotent_and_errors_after(wotlk_settings):
    fs = wowlib.fs.FileSystem.open(wotlk_settings)
    assert fs.is_open
    fs.close()
    assert not fs.is_open
    assert fs.kind == wowlib.StorageKind.Mpq  # kind survives close
    with pytest.raises(wowlib.StorageNotOpen):
        fs.read_file("DBFilesClient/Map.dbc")
    fs.close()  # second close is a no-op


def test_with_statement_scopes_the_release(wotlk_settings):
    with wowlib.fs.FileSystem.open(wotlk_settings) as fs:
        assert fs.is_open
        assert fs.read_file("DBFilesClient/Map.dbc")[:4] == b"WDBC"
    assert not fs.is_open
