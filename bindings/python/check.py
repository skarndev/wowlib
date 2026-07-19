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


# version constants and enums; storage_kind is a read-only property
check(wowlib.versions.wotlk.build == 12340, "wotlk build")
check(wowlib.versions.tww.build == 65299, "tww build")
check(wowlib.versions.wotlk.storage_kind == wowlib.StorageKind.Mpq, "kind mpq")
check(wowlib.versions.shadowlands.storage_kind == wowlib.StorageKind.Casc,
      "kind casc")
try:
    wowlib.versions.wotlk.storage_kind = wowlib.StorageKind.Casc
    check(False, "storage_kind should be read-only")
except AttributeError:
    pass
check(wowlib.Locale.enUS is not None, "locale enum")

# FileDataID: the one identity helper the facade API needs
check(wowlib.FileDataId(775971).value == 775971, "fdid value")

# FileKey: the generic file identity version-independent tools operate on
key = wowlib.FileKey("World/Maps/Azeroth/Azeroth.wdt")
check(key.path == "world\\maps\\azeroth\\azeroth.wdt", "path canonicalized")
check(key.fdid is None, "pre-CASC style key: fdid simply absent")
key = wowlib.FileKey(wowlib.FileDataId(775971))
check(key.fdid.value == 775971 and key.path is None, "id-only key")
key = wowlib.FileKey("a/b.blp", wowlib.FileDataId(7))
check(key.path == "a\\b.blp" and key.fdid.value == 7, "both-halves key")

# the surface stays minimal: internals are not welded
for absent in ("ErrorCode",):
    check(not hasattr(wowlib, absent), f"{absent} should not be welded")
for absent in ("ProjectDirectory", "CsvListfile", "CsvListfileOptions"):
    check(not hasattr(wowlib.fs, absent), f"fs.{absent} should not be welded")

# explicit lifetime control is welded from the protected C++ surface
check(hasattr(wowlib.fs.FileSystem, "close"), "close welded")
check(isinstance(wowlib.fs.FileSystem.is_open, property), "is_open property")
check(hasattr(wowlib.fs.FileSystem, "__enter__"), "context-manager protocol")

# the exception hierarchy: one class per ErrorCode under wowlib.Error, with
# curated builtin bases for idiomatic handlers
check(issubclass(wowlib.StorageOpenFailed, wowlib.Error), "code class under Error")
check(issubclass(wowlib.FileNotFound, FileNotFoundError), "FileNotFound is builtin-compatible")
check(issubclass(wowlib.ListfileIoError, OSError), "listfile io is an OSError")
check(issubclass(wowlib.NotImplemented, NotImplementedError), "builtin NotImplementedError base")

# docstrings carried through welder
check("FileDataID" in (wowlib.FileDataId.__doc__ or ""), "docstring present")
check("filesystem" in (wowlib.fs.__doc__ or "").lower(), "submodule docstring")

# --- formats: versioned classes, expansion key, factories, chunk vocabulary --

# the Expansion enum keys versioned format access and maps onto the constants
check(wowlib.Expansion.Wotlk is not None, "Expansion enum welded")
check(wowlib.to_client_version(wowlib.Expansion.Wotlk) == wowlib.versions.wotlk,
      "expansion -> version constant")
check(wowlib.to_expansion(wowlib.versions.shadowlands) == wowlib.Expansion.Shadowlands,
      "version constant -> expansion")

# flat suffixed per-version classes; shared wire structs live in formats.wmo
for name in ("WmoWotlk", "WmoShadowlands", "WmoRootWotlk", "WmoGroupShadowlands",
             "WmoBatchWotlk", "WmoGroupHeaderShadowlands", "StringBlock",
             "ChunkBlob", "C3Vector", "CAaBox"):
    check(hasattr(wowlib.formats, name), f"formats.{name} welded")
for name in ("SmoMaterial", "SmoHeader", "SmoDoodadDef", "CAaBspNode", "LightType"):
    check(hasattr(wowlib.formats.wmo, name), f"formats.wmo.{name} welded")

# a fresh entity carries its wire defaults; round-trip internals stay hidden
wmo_fresh = wowlib.formats.WmoWotlk()
check(wmo_fresh.root.mver == 17, "fresh root MVER 17")
check(not hasattr(wmo_fresh.root, "journal"), "chunk_extras internals not welded")
check(not hasattr(wowlib.formats.WmoWotlk, "parse"), "span-of-spans parse not welded")

# the string_block API: offsets stay stable, entries view the blob
block = wowlib.formats.StringBlock()
offset = block.add("textures/stone.blp")
check(offset == 0 and block.at(offset) == "textures/stone.blp", "string_block add/at")
check(block.entries()[0] == (0, "textures/stone.blp"), "string_block entries")

# factory overloads are typed stubs; the runtime rejects unbuilt versions with
# the generated exception class
check(callable(wowlib.formats.load_wmo), "load_wmo factory")
check(callable(wowlib.formats.convert_wmo), "convert_wmo factory")

# mypy narrowing: Literal[Expansion.*] overloads resolve to the exact class
# (runs when mypy is importable; the stubs ship next to the module)
_stubs = os.path.join(os.path.dirname(os.path.dirname(wowlib.__file__)), "stubs")
if not os.path.isdir(_stubs):
    _stubs = os.path.join(os.path.dirname(wowlib.__file__), "stubs")
try:
    import mypy.api as _mypy_api
except ImportError:
    print("note: mypy not importable, narrowing check skipped")
else:
    if os.path.isdir(_stubs):
        import tempfile

        _probe = (
            "import wowlib\n"
            "import wowlib.formats\n"
            "def probe(fs: wowlib.fs.FileSystem) -> None:\n"
            "    w = wowlib.formats.load_wmo(fs, wowlib.FileKey('a.wmo'),\n"
            "                                wowlib.Expansion.Wotlk)\n"
            "    reveal_type(w)\n"
            "    s = wowlib.formats.load_wmo(fs, wowlib.FileKey('a.wmo'),\n"
            "                                wowlib.Expansion.Shadowlands)\n"
            "    reveal_type(s)\n"
        )
        with tempfile.TemporaryDirectory() as _tmp:
            _probe_path = os.path.join(_tmp, "probe.py")
            with open(_probe_path, "w") as f:
                f.write(_probe)
            os.environ["MYPYPATH"] = _stubs
            _out, _err, _ = _mypy_api.run(["--no-error-summary", _probe_path])
        check('Revealed type is "wowlib.formats.WmoWotlk"' in _out,
              f"mypy narrows Wotlk overload:\n{_out}{_err}")
        check('Revealed type is "wowlib.formats.WmoShadowlands"' in _out,
              f"mypy narrows Shadowlands overload:\n{_out}{_err}")

# settings are an immutable value: keyword construction with NSDMI defaults,
# read-only fields
settings = wowlib.fs.FileSystemSettings(client_path="/no/such/client",
                                        version=wowlib.versions.wotlk)
check(settings.casc_product == "wow", "NSDMI default carried")
check(settings.locale is None, "optional defaults to None")
check(settings.custom_fdid_start.value == 1_000_000_000, "fdid start default")
try:
    settings.client_path = "/elsewhere"
    check(False, "settings fields should be read-only")
except AttributeError:
    pass
# a keyword can skip past earlier defaulted fields
check(wowlib.fs.FileSystemSettings("/c", wowlib.versions.wotlk,
                                   casc_product="wowt").casc_product == "wowt",
      "keyword skips middle defaults")

# error translation: opening a nonexistent client raises the typed exception
try:
    wowlib.fs.FileSystem.open(settings)
    check(False, "open should have raised")
except wowlib.StorageOpenFailed as error:
    check(error.code == "StorageOpenFailed", f"code attribute: {error.code}")
    check(isinstance(error, wowlib.Error), "caught through the base too")
    check(isinstance(error.native_error, int), "native_error attribute")

# real-client round-trip (opt-in via env)
clients = os.environ.get("WOWLIB_TEST_CLIENTS_DIR")
if clients:
    settings = wowlib.fs.FileSystemSettings(
        client_path=os.path.join(clients, "World of Warcraft 3.3.5a"),
        version=wowlib.versions.wotlk)
    fs = wowlib.fs.FileSystem.open(settings)
    check(fs.kind == wowlib.StorageKind.Mpq, "facade kind property")
    data = fs.read_file("DBFilesClient/Map.dbc")
    check(isinstance(data, bytes) and data[:4] == b"WDBC", "Map.dbc magic via Python")

    # the generic FileKey identity works across the facade
    check(fs.read_file(wowlib.FileKey("DBFilesClient/Map.dbc")) == data,
          "same bytes via FileKey")
    check(fs.exists(wowlib.FileKey("DBFilesClient/Map.dbc")), "exists via FileKey")
    resolved = fs.resolve(wowlib.FileKey("DBFilesClient/Map.dbc"))
    check(resolved.path == "dbfilesclient\\map.dbc", "resolve canonicalizes")
    check(resolved.fdid is None, "MPQ era: resolve leaves fdid absent")

    # explicit close: deterministic release, typed error afterwards, idempotent
    check(fs.is_open, "open filesystem reports is_open")
    fs.close()
    check(not fs.is_open, "closed filesystem reports not is_open")
    check(fs.kind == wowlib.StorageKind.Mpq, "kind survives close")
    try:
        fs.read_file("DBFilesClient/Map.dbc")
        check(False, "read after close should raise")
    except wowlib.StorageNotOpen:
        pass
    fs.close()  # second close is a no-op

    # the with-statement scopes the release
    with wowlib.fs.FileSystem.open(settings) as fs2:
        check(fs2.is_open, "with-block filesystem is open")
        check(fs2.read_file("DBFilesClient/Map.dbc") == data, "same bytes in with-block")
    check(not fs2.is_open, "with-block exit closed the filesystem")

    # formats end-to-end: the factory loads a real WMO, the classmethod agrees,
    # and an untouched entity rewrites byte-for-byte through Python
    with wowlib.fs.FileSystem.open(settings) as fs3:
        wmo_path = "World/wmo/Azeroth/Buildings/GoldshireInn/GoldshireInn.wmo"
        if fs3.exists(wmo_path):
            wmo = wowlib.formats.load_wmo(fs3, wowlib.FileKey(wmo_path),
                                          wowlib.Expansion.Wotlk)
            check(type(wmo).__name__ == "WmoWotlk", "factory returns the exact class")
            check(len(wmo.groups) == wmo.root.header.n_groups, "all groups loaded")
            check(wmo.root.textures.at(wmo.root.materials[0].texture_1).endswith(".blp"),
                  "material texture resolves through the string block")
            check(wmo.write_root() == fs3.read_file(wmo_path),
                  "byte-perfect rewrite via Python")
            try:
                wowlib.formats.load_wmo(fs3, wowlib.FileKey(wmo_path),
                                        wowlib.Expansion.Cata)
                check(False, "unbuilt expansion should raise")
            except wowlib.UnsupportedClientVersion:
                pass
            print("formats end-to-end OK,", len(wmo.groups), "groups")

    print("real-client round-trip OK,", len(data), "bytes")

print("wowlib python module OK")