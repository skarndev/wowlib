"""The buffer protocol (buffers.cpp): read/write speak whole entities and accept
bytes, bytes-like and file-like sources/sinks, and the write() length guard rejects
a group-count mismatch. We assert the plumbing, not the WMO wire format itself."""

import io

import pytest

import wowlib
from wowlib.formats import wmo as wmo_mod


def test_write_emits_the_root_chunk_stream(fresh_wmo):
    sink = io.BytesIO()
    fresh_wmo.write(sink, [])
    assert sink.getvalue()[:4] == b"REVM"


@pytest.mark.parametrize("wrap", [bytes, bytearray, io.BytesIO])
def test_read_accepts_bytes_byteslike_and_filelike(fresh_wmo, wrap):
    sink = io.BytesIO()
    fresh_wmo.write(sink, [])
    raw = sink.getvalue()

    loaded = wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)
    loaded.read(wrap(raw), [])
    assert loaded.root.mver == 17
    assert len(loaded.groups) == 0


def test_write_rejects_group_count_mismatch(fresh_wmo):
    with pytest.raises(ValueError):
        fresh_wmo.write(io.BytesIO(), [io.BytesIO()])
