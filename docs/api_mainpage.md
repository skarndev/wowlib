# wowlib — C++ API reference {#mainpage}

This is the **generated C++ reference** for wowlib: the classes, templates,
namespaces and files under `src/wowlib/`, read straight from the real headers
through welder's own
[Doxygen INPUT_FILTER](https://github.com/skarndev/welder/blob/main/tools/welder_doxygen_filter.py),
so the `[[=welder::doc/returns/tparam]]` annotations that also drive the Python
docstrings come through here too.

> **New to wowlib?** Read the narrative **Guide** first — it explains *why* each
> piece exists, with runnable examples. This reference is the exhaustive *what*.
> The **Python API** is documented separately in the guide (generated from the
> `.pyi` stub tree).

## Where to start

- **`wowlib::fs`** — the filesystem gateway: one interface over MPQ (pre-WoD) and
  CASC (WoD+) storages, plus the listfile / FileDataID machinery.
- **`wowlib::formats`** — the chunk framework and the versioned world-file
  entities. `formats/wmo/` is the first fully-modelled format (root + group,
  nested chunk structs).
- **`wowlib::core`** — the shared vocabulary: `ClientVersion`, `Expansion`,
  file keys, buffers, errors.

## How this reference is built

wowlib's headers carry Doxygen `/** … */` blocks *and* `[[=welder::doc]]`
annotations; the annotations flow in through the same welder filter that powers
the runtime docstrings, so a single annotation documents both the C++ reference
and the bound Python surface. The reference is generated with `EXTRACT_ALL` so
the full structure is browsable, with a source browser throughout.
