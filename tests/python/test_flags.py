"""Bitmask enums ([[=welder::flags]]) bind as enum.IntFlag.

Flag fields are typed as their enums rather than raw ints; real client files
carry OR-combined values and occasionally undocumented bits, so the binding
must CONVERT those rather than raise (the plain IntEnum behavior). Int
compatibility is preserved — IntFlag inherits int, so existing ``flags & 0x4``
call sites keep working.
"""

from __future__ import annotations

import enum

import pytest

import wowlib
from wowlib.formats import adt, wmo


def test_flag_enums_bind_as_intflag() -> None:
    assert issubclass(wmo.group.chunks.GroupFlags, enum.IntFlag)
    assert issubclass(adt.chunks.LayerFlags, enum.IntFlag)
    assert issubclass(wowlib.formats.m2.root.GlobalFlags, enum.IntFlag)
    # A non-bitmask enum stays a plain IntEnum.
    assert not issubclass(adt.AlphaFormat, enum.IntFlag)
    assert issubclass(adt.AlphaFormat, enum.IntEnum)


def test_flag_fields_are_enum_typed_and_combinable() -> None:
    GF = wmo.group.chunks.GroupFlags
    body = wmo.group.WMOGroupBodyVanillaToWotlk()
    assert isinstance(body.header.flags, GF)

    body.header.flags = GF.Exterior | GF.HasVertexColors
    assert body.header.flags & GF.Exterior
    assert GF.HasVertexColors in body.header.flags
    # int compatibility: the pre-IntFlag call-site style keeps working.
    assert body.header.flags & 0x4

    # Undocumented bits (real files carry them) survive the round trip —
    # exactly what the old IntEnum binding raised ValueError on.
    body.header.flags = GF(0x80000000 | int(GF.Exterior))
    assert int(body.header.flags) & 0x80000000


def test_flag_fields_keep_the_numpy_view() -> None:
    """A record gaining an enum field must not fall off the zero-copy
    structured-array path: the field views as its underlying integer
    (NumPy has no enum dtype)."""
    np = pytest.importorskip("numpy")
    body = wmo.group.WMOGroupBodyVanillaToWotlk()
    poly = wmo.group.chunks.SMOPoly()
    poly.flags = wmo.group.chunks.PolyFlags(0x20)
    body.polys.append(poly)
    a = np.asarray(body.polys)
    assert a.dtype["flags"] == np.uint8  # PolyFlags' underlying integer
    assert not a.flags["OWNDATA"]  # still zero-copy
    assert int(a[0]["flags"]) == 0x20
