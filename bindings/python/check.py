"""Smoke check for the wowlib Python extension (run by ctest with PYTHONPATH set).

Verifies the welded surface end-to-end: the fs/versions submodules (mirroring the
C++ namespaces), types, enums, constructors, docstrings, error translation, and —
when WOWLIB_TEST_CLIENTS_DIR is set — a real client round-trip through the
FileSystem facade.
"""

import io
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
check(wowlib.FileDataID(775971).value == 775971, "fdid value")

# FileKey: the generic file identity version-independent tools operate on
key = wowlib.FileKey("World/Maps/Azeroth/Azeroth.wdt")
check(key.path == "world\\maps\\azeroth\\azeroth.wdt", "path canonicalized")
check(key.fdid is None, "pre-CASC style key: fdid simply absent")
key = wowlib.FileKey(wowlib.FileDataID(775971))
check(key.fdid.value == 775971 and key.path is None, "id-only key")
key = wowlib.FileKey("a/b.blp", wowlib.FileDataID(7))
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
check("FileDataID" in (wowlib.FileDataID.__doc__ or ""), "docstring present")
check("filesystem" in (wowlib.fs.__doc__ or "").lower(), "submodule docstring")

# --- formats: versioned classes, expansion key, factories, chunk vocabulary --

# the Expansion enum keys versioned format access and maps onto the constants
check(wowlib.Expansion.Wotlk is not None, "Expansion enum welded")
check(wowlib.to_client_version(wowlib.Expansion.Wotlk) == wowlib.versions.wotlk,
      "expansion -> version constant")
check(wowlib.to_expansion(wowlib.versions.shadowlands) == wowlib.Expansion.Shadowlands,
      "version constant -> expansion")

# the module mirrors the C++ namespaces: the WMO assembly + its per-version
# classes in wmo, WMORoot in wmo.root, groups in wmo.group, and the wire structs
# in wmo.chunks / wmo.group_chunks; common vocabulary stays in formats
wmo_mod = wowlib.formats.wmo
root_mod = wmo_mod.root
group_mod = wmo_mod.group
chunks_mod = wmo_mod.chunks
gchunks_mod = wmo_mod.group_chunks
for name in ("WMOVanilla", "WMOWotlk", "WMOShadowlands", "WMOTheWarWithin"):
    check(hasattr(wmo_mod, name), f"formats.wmo.{name} welded")
check(hasattr(root_mod, "WMORootWotlk"), "formats.wmo.root.WMORootWotlk welded")
check(hasattr(group_mod, "WMOGroupShadowlands") and hasattr(group_mod, "WMOGroupBodyWotlk"),
      "formats.wmo.group.* welded")
check(hasattr(gchunks_mod, "WMOBatchWotlk") and hasattr(gchunks_mod, "WMOGroupHeaderShadowlands"),
      "formats.wmo.group_chunks.* welded")
for name in ("StringBlock", "ChunkBlob", "C3Vector", "CAaBox"):
    check(hasattr(wowlib.formats, name), f"formats.{name} welded")
for name in ("SMOMaterial", "SMOHeader", "SMODoodadDef", "LightType", "MaterialFlags",
             "NewLight", "AmbientVolume"):
    check(hasattr(chunks_mod, name), f"formats.wmo.chunks.{name} welded")
for name in ("CAaBspNode", "GroupFlags", "SMOPoly", "PointLight"):
    check(hasattr(gchunks_mod, name), f"formats.wmo.group_chunks.{name} welded")

# flag enums are IntEnums; bit tests work against the plain integer fields
check(gchunks_mod.GroupFlags.exterior == 0x8, "flag enum bit value")

# the facade: a welded empty C++ base per version-differing family gives REAL
# native inheritance and isinstance (no ABC glue), plus a for_version constructor
check(hasattr(wmo_mod, "WMO") and hasattr(root_mod, "WMORoot")
      and hasattr(group_mod, "WMOGroup") and hasattr(group_mod, "WMOGroupBody")
      and hasattr(gchunks_mod, "WMOGroupHeader") and hasattr(gchunks_mod, "WMOBatch"),
      "facade bases present in their submodules")
wmo = wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)
check(type(wmo).__name__ == "WMOWotlk", "for_version returns the concrete class")
check(wmo_mod.WMOWotlk.__bases__ == (wmo_mod.WMO,), "concrete natively inherits the base")
check(isinstance(wmo, wmo_mod.WMO), "isinstance against the base is real")
check(issubclass(wmo_mod.WMOWotlk, wmo_mod.WMO), "concrete is a real subclass")
check(not isinstance(wmo, root_mod.WMORoot), "families do not cross-inherit")
check(isinstance(gchunks_mod.WMOGroupHeader.for_version(wowlib.Expansion.Shadowlands),
                 gchunks_mod.WMOGroupHeader), "wire-struct family facade too")

# the AnyX union aliases are REAL, importable runtime objects (types.UnionType),
# not merely stub-only names: each family exposes Any<Family> on its submodule,
# usable in annotations and — since they union the concrete classes — isinstance
import types as _types
from wowlib.formats import wmo as _wmo_import
check(isinstance(_wmo_import.AnyWMO, _types.UnionType), "AnyWMO is an importable runtime TypeAlias")
check(all(isinstance(getattr(_mod, _name), _types.UnionType) for _mod, _name in (
    (wmo_mod, "AnyWMO"), (root_mod, "AnyWMORoot"),
    (group_mod, "AnyWMOGroup"), (group_mod, "AnyWMOGroupBody"),
    (gchunks_mod, "AnyWMOGroupHeader"), (gchunks_mod, "AnyWMOBatch"))),
    "every family exposes its AnyX union on its submodule")
check(isinstance(wmo, wmo_mod.AnyWMO), "a concrete WMO isinstance-matches the AnyWMO union")

# a fresh entity carries its wire defaults; round-trip internals stay hidden
check(wmo.root.mver == 17, "fresh root MVER 17")
check(not hasattr(wmo.root, "journal"), "ChunkExtras internals not welded")
check(all(hasattr(wmo, verb) for verb in ("read", "write", "convert")),
      "WMO speaks read/write/convert (inherited from the welded base)")
check("read" not in wmo_mod.WMOWotlk.__dict__,
      "the assembly's read/write live on the base, not re-welded on the concrete")
check(hasattr(wmo.root, "read") and hasattr(wmo.root, "write"),
      "sub-entity read/write methods welded on the concrete")

# read/write speak whole entities and accept bytes, bytes-like and file-like:
# round-trip an empty WMO through a BytesIO sink and back three ways
_sink = io.BytesIO()
wmo.write(_sink, [])
_root_bytes = _sink.getvalue()
check(_root_bytes[:4] == b"REVM", "write() emits the root chunk stream")
for _src in (_root_bytes, bytearray(_root_bytes), io.BytesIO(_root_bytes)):
    _w2 = wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)
    _w2.read(_src, [])
    check(_w2.root.mver == 17 and len(_w2.groups) == 0,
          "read() accepts bytes, bytes-like and file-like")
# write() length guard: one output per group file
try:
    wmo.write(io.BytesIO(), [io.BytesIO()])
    check(False, "write() should reject a group-count mismatch")
except ValueError:
    pass
# convert is a method now: identity copies, stepless cross-version raises
check(type(wmo.convert(wowlib.Expansion.Wotlk)).__name__ == "WMOWotlk", "identity convert")
try:
    wmo.convert(wowlib.Expansion.Shadowlands)
    check(False, "stepless cross-version convert should raise")
except wowlib.NotImplemented:
    pass

# the StringBlock API: decoded entries, offsets stay stable
block = wowlib.formats.StringBlock()
offset = block.add("textures/stone.blp")
check(offset == 0 and block.at(offset) == "textures/stone.blp", "StringBlock add/at")
entry = block.entries()[0]
check(entry.offset == 0 and entry.value == "textures/stone.blp", "StringBlock entries")

# mypy narrowing: for_version(Literal[Expansion.*]) resolves to the exact class,
# widening the result to the AnyWMO union is accepted with no cast, and convert()
# narrows on the target Literal. read/write buffer overloads type-check too.
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
            "import io\n"
            "import wowlib\n"
            "from wowlib.formats import wmo\n"
            "w = wmo.WMO.for_version(wowlib.Expansion.Wotlk)\n"
            "reveal_type(w)\n"
            "s: wmo.AnyWMO = wmo.WMO.for_version(wowlib.Expansion.Shadowlands)\n"
            "reveal_type(s)\n"
            "c = w.convert(wowlib.Expansion.Cata)\n"
            "reveal_type(c)\n"
            "w.read(io.BytesIO(b''), [io.BytesIO(b'')])\n"
            "w.write(io.BytesIO(), [])\n"
        )
        with tempfile.TemporaryDirectory() as _tmp:
            _probe_path = os.path.join(_tmp, "probe.py")
            with open(_probe_path, "w") as f:
                f.write(_probe)
            os.environ["MYPYPATH"] = _stubs
            _out, _err, _ = _mypy_api.run(["--no-error-summary", _probe_path])
        check('Revealed type is "wowlib.formats.wmo.WMOWotlk"' in _out,
              f"mypy narrows for_version(Wotlk) -> WMOWotlk:\n{_out}{_err}")
        check('Revealed type is "wowlib.formats.wmo.WMOCata"' in _out,
              f"mypy narrows convert(Cata) -> WMOCata:\n{_out}{_err}")
        check("WMOShadowlands" in _out,
              f"AnyWMO assignment reveals the union:\n{_out}{_err}")
        # the widening / buffer overloads must not raise a type error in the probe
        # (the unrelated __eq__ LSP notes live in __init__.pyi, not probe.py)
        _probe_errors = [ln for ln in _out.splitlines()
                         if "probe.py" in ln and "error:" in ln]
        _joined = "\n".join(_probe_errors)
        check(not _probe_errors, f"the facade probe type-checks clean:\n{_joined}")

# settings are an immutable value: keyword construction with NSDMI defaults,
# read-only fields
settings = wowlib.fs.FileSystemSettings(client_path="/no/such/client",
                                        version=wowlib.versions.wotlk)
check(settings.casc_product == "wow", "NSDMI default carried")
check(settings.locale == wowlib.Locale.enUS, "locale defaults to enUS")
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

    # formats end-to-end through the facade: for_version + read(fs, key) load a
    # real WMO, an untouched entity rewrites byte-for-byte into buffers, and
    # convert() dispatches on the target
    with wowlib.fs.FileSystem.open(settings) as fs3:
        wmo_path = "World/wmo/Azeroth/Buildings/GoldshireInn/GoldshireInn.wmo"
        if fs3.exists(wmo_path):
            loaded = wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)
            loaded.read(fs3, wowlib.FileKey(wmo_path))
            check(type(loaded).__name__ == "WMOWotlk", "for_version + read load the exact class")
            check(isinstance(loaded, wmo_mod.WMO), "the loaded WMO isinstance the base")
            check(len(loaded.groups) == loaded.root.header.n_groups, "all groups loaded")
            check(loaded.root.textures.at(loaded.root.materials[0].texture_1)
                  .lower().endswith(".blp"),
                  "material texture resolves through the string block")
            # byte-perfect rewrite via Python: write into buffers, one per group
            root_sink = io.BytesIO()
            group_sinks = [io.BytesIO() for _ in loaded.groups]
            loaded.write(root_sink, group_sinks)
            check(root_sink.getvalue() == fs3.read_file(wmo_path),
                  "byte-perfect root rewrite via Python")
            # every expansion has an instantiation; Cata reads the same 3.3.5 file
            cata = wmo_mod.WMO.for_version(wowlib.Expansion.Cata)
            cata.read(fs3, wowlib.FileKey(wmo_path))
            check(type(cata).__name__ == "WMOCata", "per-expansion facade dispatch")
            # convert: identity copies, a stepless pair raises NotImplemented
            check(type(loaded.convert(wowlib.Expansion.Wotlk)).__name__ == "WMOWotlk",
                  "identity convert")
            try:
                loaded.convert(wowlib.Expansion.Shadowlands)
                check(False, "stepless cross-version convert should raise")
            except wowlib.NotImplemented as error:
                check("Wotlk -> Shadowlands" in str(error),
                      f"convert error names the pair: {error}")
            print("formats end-to-end OK,", len(loaded.groups), "groups")

    print("real-client round-trip OK,", len(data), "bytes")

print("wowlib python module OK")