# Getting started

## Prerequisites

wowlib targets **C++26 reflection**, currently only implemented by the
**gcc-16** toolchain. For the Python extension you also need **CPython ≥ 3.13**.
Dependencies (StormLib, CascLib, welder, nanobind) are fetched by CMake — no
manual install step.

## Build the C++ library

```bash
cmake --preset gcc16-debug
cmake --build --preset gcc16-debug
ctest --preset gcc16-debug        # run the C++ test suite
```

## Build the Python extension

The bindings preset turns on `WOWLIB_BUILD_PYTHON`, which builds the stable-ABI
extension and its `.pyi` stub tree:

```bash
cmake --preset gcc16-bindings
cmake --build --preset gcc16-bindings
```

Or install it (editable) straight into your environment — scikit-build-core
reuses the same `build/bindings` tree:

```bash
pip install -e ".[dev]"
```

```python
import wowlib
print(wowlib.__version__ if hasattr(wowlib, "__version__") else "wowlib loaded")
```

## Build the documentation

The docs site (this guide + the Python API + the Doxygen C++ reference) is built
by `docs/build.py`, run from the project `.venv`:

```bash
# one-time: add the docs toolchain to your environment
pip install ".[docs]"

.venv/bin/python docs/build.py serve    # live-reload at http://127.0.0.1:8000/
.venv/bin/python docs/build.py build    # -> build/docs/site/index.html
```

The C++ reference is generated with [Doxygen](https://www.doxygen.nl/) (found on
your `PATH`) through welder's annotation filter; the Python API is rendered from
the `.pyi` stubs, so **build the `wowlib_pyi` target first** or those pages come
out empty.
