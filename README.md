# wowlib

[![ci-linux](https://github.com/skarndev/wowlib/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/skarndev/wowlib/actions/workflows/ci-linux.yml)
[![ci-macos](https://github.com/skarndev/wowlib/actions/workflows/ci-macos.yml/badge.svg)](https://github.com/skarndev/wowlib/actions/workflows/ci-macos.yml)
[![ci-windows](https://github.com/skarndev/wowlib/actions/workflows/ci-windows.yml/badge.svg)](https://github.com/skarndev/wowlib/actions/workflows/ci-windows.yml)
[![docs](https://github.com/skarndev/wowlib/actions/workflows/docs.yml/badge.svg)](https://skarndev.github.io/wowlib/)

**wowlib** reads and writes World of Warcraft client files — a modern **C++26**
core, automatically bound to **Python** (and, later, Lua) by
[welder](https://github.com/skarndev/welder). It exists to make community
tooling — map viewers, model viewers, Blender add-ons, exploration projects —
possible on top of one shared, well-tested file layer.

## Highlights

- **Filesystem gateway** — one interface over **MPQ** (pre-WoD clients, via
  [StormLib](https://github.com/ladislav-zezula/StormLib)) and **CASC** (WoD+
  clients, via [CascLib](https://github.com/ladislav-zezula/CascLib)), with
  client patch-chain resolution, listfile / FileDataID machinery and a
  project-directory overlay for modding workflows.
- **Reflection-driven format framework** — every chunked format is declared
  once as annotated C++26 structs; parsing, writing, per-expansion version
  ranges, the Python bindings and the documentation are all derived from that
  single declaration.
- **Round-trip guarantees** — formats whose containers allow it are
  **byte-perfect** (`write(read(bytes)) == bytes`); offset-table formats are
  **semantic** (`read(write(x)) == x`, canonical relayout on write). Unknown
  and undocumented chunks are preserved verbatim, never dropped.
- **ClientDB** — every database container era from **WDBC** through **WDC5**,
  with typed record structs for **1221 tables** generated from
  [WoWDBDefs](https://github.com/wowdev/WoWDBDefs) across all supported client
  versions.
- **Typed Python bindings** — a stable-ABI (`abi3`) extension for CPython
  ≥ 3.13 with rich `.pyi` stubs and zero-copy NumPy views over WoW vectors.
  MSVC-ABI-compatible builds for use inside Blender on Windows.

## Supported clients & format status

wowlib targets the **last minor release of every major expansion**, Vanilla
through The War Within. Formats are modelled for all versions in one entity;
the matrix below shows how far each has been *verified*:

- ✅ — implemented, round-trip verified against a real client's file corpus
- 🟡 — implemented, awaiting a client install to verify against
- ➖ — not applicable to that client

✅ file-format cells are backed by the [nightly exhaustive audit](.github/workflows/ci-audit.yml):
every file of every format in every installed client (~3.4M files) is
round-tripped, with the handful of shipped-corrupt originals catalogued in the
audit report artifact.

| Format | Round-trip | Vanilla<br>1.12.1 | TBC<br>2.4.3 | WotLK<br>3.3.5a | Cata<br>4.3.4 | MoP<br>5.4.8 | WoD<br>6.2.3 | Legion<br>7.3.5 | BfA<br>8.3.7 | SL<br>9.2.7 | DF<br>10.2.7 | TWW<br>11.2.7 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **WMO** (root + groups) | byte-perfect | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 |
| **M2** (+ `.skin` `.anim` `.skel` `.bone`)¹ | semantic | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 |
| **ADT** (root + split files)² | semantic | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 |
| **WDT** (+ `_occ` `_lgt` `_fogs` `_mpv`) | byte-perfect | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 |
| **WDL** | byte-perfect | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 |
| **BLP**³ | byte-perfect | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 |
| **DBC / DB2**⁴ | byte-perfect (WDBC/WDB2), semantic (WDC*) | ✅ WDBC | ✅ WDBC | ✅ WDBC | ✅ WDB2 | 🟡 WDB2 | 🟡 WDB2 | 🟡 WDC1 | 🟡 WDC3 | ✅ WDC3 | 🟡 WDC5 | 🟡 WDC5 |
| **MPQ** storage & patch chain⁵ | — | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ➖ | ➖ |
| **CASC** storage | — | ➖ | ➖ | ➖ | ➖ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 |

¹ `.phys` is carried as an opaque blob for now (structured records planned).
Semantic round-trip means the writer relays out offset tables canonically, so
output is equivalent, not byte-identical.
² Monolithic (WotLK-) and split (Cata+ `_tex0`/`_obj0`/`_obj1`/`_lod`) layouts
both supported; `_obj1`/`_lod` LOD chunks currently round-trip verbatim as raw
chunks, structured access planned.
³ BLP is identical across all client versions; verification spans every
installed client's full texture corpus. JPEG-encoded BLPs round-trip verbatim
but do not decode.
⁴ WDC4 (10.1–10.2.5) is implemented as well. Encrypted (TACT) DB2 sections are
preserved and reported; key injection is wired but not yet exercisable locally.
⁵ The 3.3.5a patch chain was verified against the client binary itself
(reverse-engineered); the 4.3.4 and 5.4.8 UpdateChain-era tables have
dedicated integration tests against their installs.

### Classic clients

WoW Classic Era, the Classic progression realms (BCC, WotLK, Cataclysm, MoP
Classic) and the Anniversary realms are **not** old clients — they are the
modern client rebuilt from whatever retail branch was current, shipping
old-looking content. Classic Era 1.15.9 is a Midnight-era CASC client; Cata
Classic 4.4.2 writes War Within-era files. One version *number* can even span
two engines (4.4.0 shipped on Dragonflight and, months later, on The War
Within), and `wow_classic_titan` calls itself 3.80.

wowlib keys these off the **build number** — Blizzard's counter is global
across every product — via a `flavor` on `ClientVersion` and the
`format_lineage` it implies, so a Classic client resolves to the retail column
above that its files actually match. `ClientInstall.detect` reads the exact
version and product code off an installation. No Classic install is available
locally, so the mapping is covered by unit tests rather than a file corpus.

### Not yet implemented

Structured M2 `.phys` records · structured ADT `_obj1`/`_lod` (and blend-mesh)
chunks · TACT decryption of encrypted DB2 sections · cross-version format
conversion (scaffolding only) · Lua bindings (annotated throughout, module
deferred) · hotfix caches (`DBCache.bin`) · standalone formats not yet started
(`.tex`, `.lit`, `.wlw`/`.wlq`/`.wlm`, …).

## Quick start

Prebuilt packages need no toolchain — the compiler requirement below is only
for building from source:

```bash
pip install wowlib-py            # then: import wowlib
dotnet add package Wowlib        # C#/.NET
```

(The PyPI *distribution* is `wowlib-py` because `wowlib` there belongs to an
unrelated, long-abandoned project; the module you import is `wowlib`.)

Building from source requires **gcc ≥ 16** (currently the only toolchain
implementing C++26 reflection); on **macOS specifically, gcc ≥ 16.2** — 16.1's
Darwin port silently corrupts string literals placed after weak definitions
([GCC PR 126723](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=126723)), which
reflection-heavy code hits constantly, and the configure refuses it. All
dependencies are fetched by CMake — no manual installs.

```bash
# C++ library + tests
cmake --preset gcc16-debug
cmake --build --preset gcc16-debug
ctest --preset gcc16-debug

# Python extension (stable-ABI, CPython ≥ 3.13) + .pyi stubs
cmake --preset gcc16-bindings
cmake --build --preset gcc16-bindings
# …or straight into your environment:
pip install -e ".[dev]"
```

```python
import wowlib
from wowlib.formats import wmo

root = wmo.WMOWotlk.read(open("Building.wmo", "rb").read())
for material in root.materials:
    ...

group = wmo.WMOWotlk.read_group(group_data)
verts = group.vertices          # zero-copy numpy.ndarray view
```

```cpp
#include <wowlib/formats/wmo/wmo.hpp>

auto root = wowlib::formats::wmo::WMO<Expansion::Wotlk>::read(bytes);
for (auto const& mat : root.materials()) { /* … */ }
```

## Testing

The unit suite (Catch2 + pytest) runs anywhere — that is what CI runs.
Integration tests validate round-trips against real client installs and
self-skip unless you point them at one:

```bash
export WOWLIB_TEST_CLIENTS_DIR=/path/to/your/clients   # folders with WoW installs
export WOWLIB_TEST_LISTFILE=/path/to/community-listfile.csv
ctest --preset gcc16-debug
```

## Documentation

The docs site (guide + typed Python API + Doxygen C++ reference) is hosted at
**[skarndev.github.io/wowlib](https://skarndev.github.io/wowlib/)** — start
with [Getting started](https://skarndev.github.io/wowlib/guide/getting-started/).
To build it locally:

```bash
pip install ".[docs]"
python docs/build.py serve      # live-reload at http://127.0.0.1:8000/
```

## Related projects

- [welder](https://github.com/skarndev/welder) — the reflection-based binding
  generator that produces wowlib's Python (and future Lua) surface.
- [wowdev.wiki](https://wowdev.wiki/) — the community file-format
  documentation this library is checked against (and occasionally corrects).
- [WoWDBDefs](https://github.com/wowdev/WoWDBDefs) — column definitions for
  every client database table.
- [pywowlib](https://github.com/wowdev/pywowlib) /
  [WoWLib](https://github.com/skarndev/WoWLib) — earlier iterations of this
  effort, superseded by this library.

## License

[MIT](LICENSE).
