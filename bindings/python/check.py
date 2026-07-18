"""Smoke check for the wowlib Python extension (run by ctest with PYTHONPATH set).

Verifies the welded surface end-to-end: the fs/versions submodules (mirroring the
C++ namespaces), types, enums, constructors, docstrings, error translation, and —
when WOWLIB_TEST_CLIENTS_DIR is set — a real client round-trip through the
FileSystem facade.
"""

import os
import sys

import wowlib


def check(condition, message):
    if not condition:
        print("FAIL:", message)
        sys.exit(1)


# version constants and enums
check(wowlib.versions.wotlk.build == 12340, "wotlk build")
check(wowlib.versions.tww.build == 65299, "tww build")
check(wowlib.versions.wotlk.storage_kind() == wowlib.StorageKind.Mpq, "kind mpq")
check(wowlib.versions.shadowlands.storage_kind() == wowlib.StorageKind.Casc,
      "kind casc")
check(wowlib.Locale.enUS is not None, "locale enum")

# FileKey constructor overloads + canonicalization
key = wowlib.FileKey("World/Maps/Azeroth/Azeroth.wdt")
check(key.path == "world\\maps\\azeroth\\azeroth.wdt", "path canonicalized")
check(key.fdid is None, "path-only key")
key = wowlib.FileKey(wowlib.FileDataId(775971))
check(key.fdid.value == 775971, "id-only key")
key = wowlib.FileKey("a/b.blp", wowlib.FileDataId(7))
check(key.path == "a\\b.blp" and key.fdid.value == 7, "both key")

# docstrings carried through welder
check("FileDataID" in (wowlib.FileDataId.__doc__ or ""), "docstring present")
check("filesystem" in (wowlib.fs.__doc__ or "").lower(), "submodule docstring")

# error translation: opening a nonexistent client raises WowlibError
settings = wowlib.fs.FileSystemSettings()
settings.client_path = "/no/such/client"
settings.version = wowlib.versions.wotlk
try:
    wowlib.fs.FileSystem.open(settings)
    check(False, "open should have raised")
except wowlib.WowlibError as error:
    check("StorageOpenFailed" in str(error), f"error message carries the code: {error}")

# real-client round-trip (opt-in via env)
clients = os.environ.get("WOWLIB_TEST_CLIENTS_DIR")
if clients:
    settings = wowlib.fs.FileSystemSettings()
    settings.client_path = os.path.join(clients, "World of Warcraft 3.3.5a")
    settings.version = wowlib.versions.wotlk
    fs = wowlib.fs.FileSystem.open(settings)
    check(fs.kind() == wowlib.StorageKind.Mpq, "facade kind")
    data = fs.read_file("DBFilesClient/Map.dbc")
    check(isinstance(data, bytes) and data[:4] == b"WDBC", "Map.dbc magic via Python")
    print("real-client round-trip OK,", len(data), "bytes")

print("wowlib python module OK")