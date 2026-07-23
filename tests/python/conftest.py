"""Fixtures for the wowlib Python binding tests.

These tests cover the *hand-written* binding surface — error translation, the
filesystem lifetime/context-manager protocol, the buffer (read/write) protocol,
the versioned-format facade, the opaque zero-copy vectors, and the value types.
Library/format correctness itself is Catch2's job on the C++ side; we only assert
the binding contracts welder does not already guarantee, plus that the generated
type stubs resolve correctly (see the typing/ *.mypy-testing cases).
"""

import os

import pytest

import wowlib
from wowlib.formats import wmo as wmo_mod


@pytest.fixture
def fresh_wmo():
    """A fresh Wotlk WMO assembly: empty root, no groups, wire defaults."""
    return wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)


@pytest.fixture
def fresh_root(fresh_wmo):
    """The root of a fresh WMO — its many opaque vector fields start empty."""
    return fresh_wmo.root


@pytest.fixture(scope="session")
def clients_dir():
    """Path to local WoW clients; skips the test when it is not configured."""
    d = os.environ.get("WOWLIB_TEST_CLIENTS_DIR")
    if not d:
        pytest.skip("set WOWLIB_TEST_CLIENTS_DIR to run real-client tests")
    return d


@pytest.fixture
def wotlk_settings(clients_dir):
    return wowlib.fs.FileSystemSettings(
        client_path=os.path.join(clients_dir, "World of Warcraft 3.3.5a"),
        version=wowlib.versions.wotlk,
    )


@pytest.fixture
def wotlk_fs(wotlk_settings):
    fs = wowlib.fs.FileSystem.open(wotlk_settings)
    yield fs
    fs.close()
