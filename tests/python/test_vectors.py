"""Opaque containers (opaque_gen.cpp + welder's opaque-container generator): the WMO
vectors bind *by reference* — mutations and appends land on the owning C++ entity —
and the scalar vectors additionally expose a *zero-copy* numpy view via __array__.
This capability is untested by the C++ Catch2 suite (it is Python/numpy specific),
so it is asserted here directly."""

import numpy as np

from wowlib.formats.wmo.root import chunks


def test_scalar_vector_append_is_by_reference(fresh_root):
    fresh_root.group_fdids.append(7)
    fresh_root.group_fdids.append(9)
    # re-fetching the field yields the same underlying storage, not a fresh copy
    assert len(fresh_root.group_fdids) == 2
    assert list(fresh_root.group_fdids) == [7, 9]


def test_uint_vector_numpy_view_is_zero_copy(fresh_root):
    v = fresh_root.group_fdids
    v.append(7)
    v.append(9)
    arr = np.asarray(v)
    assert arr.dtype == np.uint32
    assert arr.tolist() == [7, 9]
    # zero-copy: writing through the numpy view mutates the C++ buffer in place
    arr[0] = 123
    assert v[0] == 123
    assert fresh_root.group_fdids[0] == 123


def test_float_vector_numpy_dtype(fresh_root):
    # a distinct scalar element type maps to the matching numpy dtype
    assert np.asarray(fresh_root.doodad_color_mults).dtype == np.float32


def test_struct_vector_append_lands_on_the_owner_and_preserves_state(fresh_root):
    doodad = chunks.SMODoodadDef()
    doodad.scale = 2.5
    fresh_root.doodad_defs.append(doodad)
    # append mutates the owning entity's storage (by-reference container) and copies
    # the element in by value, so its state round-trips through __getitem__.
    assert len(fresh_root.doodad_defs) == 1
    assert fresh_root.doodad_defs[0].scale == 2.5


def test_struct_vector_getitem_returns_a_live_reference(fresh_root):
    # Struct elements come back *by reference* (welder binds the opaque vector with
    # rv_policy::reference_internal), so an in-place field write through __getitem__
    # persists to the owning container. This mutability is the whole point of the
    # by-reference binding — the scalar path above proves the same for numpy views.
    fresh_root.doodad_defs.append(chunks.SMODoodadDef())
    fresh_root.doodad_defs[0].scale = 9.0
    assert fresh_root.doodad_defs[0].scale == 9.0
