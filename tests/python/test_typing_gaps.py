"""Known stub<->runtime type gaps, tracked as strict xfails.

Each asserts a contract the *runtime* already honors (the corresponding runtime
behavior is checked in test_errors.py / test_buffers.py) but the generated .pyi does
not yet express. strict=True means the day the stub generation is fixed these flip
to xpass -> a hard failure, prompting removal of the marker. They are the type-side
TODO list for the stub emitter (nb::sig / stub_patterns.nb)."""

import os
import tempfile

import mypy.api
import pytest


def _typecheck(code: str) -> str:
    """Run mypy on a snippet against the installed stubs; return combined output."""
    with tempfile.TemporaryDirectory() as d:
        probe = os.path.join(d, "probe.py")
        with open(probe, "w") as f:
            f.write(code)
        out, err, _ = mypy.api.run(["--no-error-summary", "--no-incremental", probe])
    return out + err


@pytest.mark.xfail(strict=True,
                   reason="stubs omit exception .code / .native_error (runtime provides both)")
def test_exception_attributes_are_typed():
    out = _typecheck(
        "import wowlib\n"
        "def handle(e: wowlib.StorageOpenFailed) -> None:\n"
        "    reveal_type(e.code)\n"
        "    reveal_type(e.native_error)\n"
    )
    assert "attr-defined" not in out


@pytest.mark.xfail(strict=True,
                   reason="read()/write() overloads list bytes|BinaryIO, not bytearray (runtime accepts it)")
def test_read_accepts_bytearray_at_type_check_time():
    out = _typecheck(
        "import wowlib\n"
        "from wowlib.formats import wmo\n"
        "w = wmo.WMO.for_version(wowlib.Expansion.Wotlk)\n"
        "w.read(bytearray(b''), [])\n"
    )
    assert "call-overload" not in out
