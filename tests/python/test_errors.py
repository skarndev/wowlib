"""Error translation (errors.cpp + result_casters.hpp): every C++ ErrorCode becomes
a typed exception under wowlib.Error, with curated builtin bases so idiomatic
handlers work, and Result<T> failures raise the matching typed exception carrying
its .code and .native_error. This whole layer is hand-written."""

import pytest

import wowlib


@pytest.mark.parametrize("cls_name, base", [
    ("StorageOpenFailed", wowlib.Error),
    ("FileNotFound", FileNotFoundError),
    ("ListfileIoError", OSError),
    ("NotImplemented", NotImplementedError),
])
def test_exception_bases(cls_name, base):
    assert issubclass(getattr(wowlib, cls_name), base)


def test_errorcode_enum_is_not_welded():
    assert not hasattr(wowlib, "ErrorCode")


def test_open_nonexistent_client_raises_typed_exception():
    settings = wowlib.fs.FileSystemSettings(client_path="/no/such/client",
                                            version=wowlib.versions.wotlk)
    with pytest.raises(wowlib.StorageOpenFailed) as excinfo:
        wowlib.fs.FileSystem.open(settings)
    error = excinfo.value
    assert isinstance(error, wowlib.Error)  # caught through the base too
    assert error.code == "StorageOpenFailed"
    assert isinstance(error.native_error, int)
