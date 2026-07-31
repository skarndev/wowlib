"""The BLP binding surface: the unversioned entity welds directly (no facade),
encode/decode move NumPy-visible RGBA pixels, read/write speak bytes with the
byte-perfect layout replay, and raw mip access copies payloads out. Codec
correctness itself is Catch2's job on the C++ side."""

import numpy as np
import pytest

import wowlib
from wowlib.formats import blp as blp_mod


def checker_image(width=8, height=8):
    """A small RGBA checkerboard with an alpha gradient."""
    image = blp_mod.Image()
    image.width = width
    image.height = height
    pixels = np.zeros((height, width, 4), dtype=np.uint8)
    pixels[..., 0] = 200
    pixels[1::2, ::2, 1] = 255
    pixels[..., 3] = 255
    image.pixels.extend(pixels.reshape(-1).tolist())
    return image


def test_the_entity_is_unversioned_and_constructs_bare():
    texture = blp_mod.BLP()
    assert texture.version == 1
    assert texture.mip_count == 0
    assert not hasattr(blp_mod.BLP, "for_version")


def test_encode_decode_round_trips_pixels():
    texture = blp_mod.BLP()
    texture.encode(checker_image(), blp_mod.EncodeSettings(encoding=blp_mod.ColorEncoding.Bgra))
    assert texture.color_encoding == blp_mod.ColorEncoding.Bgra
    assert texture.width == 8
    assert texture.mip_count == 4  # 8x8 .. 1x1

    decoded = texture.decode()
    array = np.asarray(decoded.pixels).reshape(decoded.height, decoded.width, 4)
    reference = np.asarray(checker_image().pixels).reshape(8, 8, 4)
    assert (array == reference).all()


def test_pixels_expose_a_zero_copy_numpy_view():
    texture = blp_mod.BLP()
    texture.encode(checker_image(), blp_mod.EncodeSettings(encoding=blp_mod.ColorEncoding.Bgra))
    decoded = texture.decode()
    array = np.asarray(decoded.pixels)
    assert array.dtype == np.uint8
    assert array.size == 8 * 8 * 4


def test_write_read_round_trips_bytes():
    texture = blp_mod.BLP()
    texture.encode(checker_image())  # default DXT/BC3 path
    data = texture.write()
    assert data[:4] == b"BLP2"

    back = blp_mod.BLP()
    back.read(data)
    assert back.write() == data
    assert back.preferred_format == blp_mod.PixelFormat.Dxt5
    assert back.mip(0) == texture.mip(0)


def test_settings_keywords_and_dxt1_selection():
    texture = blp_mod.BLP()
    texture.encode(
        checker_image(),
        blp_mod.EncodeSettings(alpha_depth=0, mipmaps=False),
    )
    assert texture.preferred_format == blp_mod.PixelFormat.Dxt1
    assert texture.mip_count == 1
    assert texture.mip_flags == 0


def test_errors_translate_to_the_reflected_hierarchy():
    texture = blp_mod.BLP()
    with pytest.raises(wowlib.InvalidEntityState):
        texture.decode()
    with pytest.raises(wowlib.FormatVersionMismatch):
        texture.read(b"\x00" * 0x494)
    with pytest.raises(wowlib.NotSupported):
        texture.encode(
            checker_image(),
            blp_mod.EncodeSettings(encoding=blp_mod.ColorEncoding.Jpeg),
        )


def test_palette_binds_by_reference():
    texture = blp_mod.BLP()
    texture.palette[0] = wowlib.formats.common.CImVector(b=1, g=2, r=3, a=0)
    assert texture.palette[0].r == 3
    assert len(texture.palette) == 256
