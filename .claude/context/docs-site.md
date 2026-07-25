# Documentation site (`docs/`)

One site, two toolchains — the same split welder uses, ported to run from the
project `.venv` (no CMake target). Driver: **`docs/build.py`** (`build` | `serve`
| `doxygen`). Deps live in `pyproject.toml` `[project.optional-dependencies].docs`
(`pip install ".[docs]"`); `doxygen` + `git` are system tools on `PATH`.

## Architecture
- **mkdocs-material** renders the guide (`docs/content/*.md`) **and** the Python
  API. Palette: blue / amber. `docs_dir = content`.
- **Python API = mkdocstrings over the `.pyi` stub tree** (NOT the compiled
  module). `mkdocs.yml` points the python handler `paths` at
  `../build/bindings/bindings/python/stubs` (relative to `mkdocs.yml`), so the
  `wowlib_pyi` build target must have run or those pages render empty (build.py
  warns). griffe loads the stubs-only tree fine; docstrings are welder-emitted
  google-style. Pages: `content/python/*.md` with `::: wowlib…` directives; the
  auto-generated `Vector*` opaque containers are filtered out of core and shown
  once (representative) on `containers.md`.
- **C++ reference = standalone Doxygen** (doxygen-awesome-css v2.4.2 theme),
  built from `src/wowlib/**` through **welder's own INPUT_FILTER**
  (`welder_doxygen_filter.py`, needs `lark` — hence lark in the docs extra, and
  build.py invokes the filter with the same `.venv` interpreter). Output goes
  out-of-source to `build/docs/reference/api`; the `inject_reference.py` mkdocs
  hook grafts it into `<site>/api` (via `WOWLIB_DOXYGEN_API` env) on every
  build/serve, so the worktree stays clean and `reference.md` links to
  `../api/index.html`. `Doxyfile.in` is `@VAR@`-substituted by build.py.

## Generic WMO/M2 field reference (engine is format_reference_impl.py;
## configs wmo_reference_config.py / m2_reference_config.py)
The WMO layout is version-parametric (`WMO<V>` per expansion), so instead of
documenting 12 near-identical per-version classes, `docs/wmo_reference.py` (an
mkdocs `on_page_markdown` hook) **generates** one generic reference. It textually
parses `src/wowlib/formats/wmo/{root/root.hpp,group/group.hpp}` for each member's
`=chunk()`, `=since(...)`, `=until(...)`, type and `=welder::doc` — since/until
spell named constants (`builds::BfA_TidesOfVengeance`), resolved through
`parse_version_constants(client_builds.hpp, <fmt>/boundaries.hpp)` (brace
literals + alias definitions; builds header must parse FIRST),
maps the version **major → expansion** (1=Vanilla … 8=BfA … 11=TWW), and fills the
`<!-- wmo-legend / wmo-root-fields / wmo-group-fields -->` markers in
`python/wmo/fields.md` with per-field **expansion-range badges** (original colored
`.exp-<key>` pills in `stylesheets/extra.css` — NOT Blizzard/wowdev icons, to avoid
copyright). `since`/`until` are the single source of truth, so ranges never drift.
Members marked `welder::mark::exclude` (texcoords, vertex_colors) are skipped; the
hook is fail-safe (parse error → visible note, build continues). This *replaced*
the earlier representative-version mkdocstrings pages (entity/root/group); the
`root-chunks`/`group-chunks` wire-struct pages remain mkdocstrings-generated.

## Key details / gotchas
- **welder filter resolution** (build.py `find_welder_filter`): `$WOWLIB_WELDER_FILTER`
  → pinned FetchContent checkout (`build/*/_deps/welder-src/tools/…`, matches the
  welded commit) → sibling `../welder/tools`. Grammar `.lark` must sit beside it.
- **Doxygen 1.17 ships no jQuery** → doxygen-awesome's JS (dark toggle) is dead.
  `patch_doxygen_header.py` injects a self-contained toggle + guide backlink
  keyed on `html.dark-mode`; `doxygen-extra.css` retunes the accent to blue. Both
  fail-safe to the stock/base theme.
- **QT_AUTOBRIEF splits `<tt>` code spans**: a `.` inside inline-`` `code` `` in
  the *first sentence* of a member brief ends the brief mid-span → unbalanced
  `<tt>`. Keep the autobrief sentence free of backtick-code-with-periods (fixed
  in `mpq_chain.hpp`). `@ref foo:` glues the trailing colon onto the target —
  keep punctuation off `@ref` (fixed in `mpq_storage.hpp`). Reference build is
  warning-clean; keep it so.
- Generated artifacts: `build/docs/` (gitignored) — awesome clone, Doxyfile,
  reference, site.

## Not yet ported from welder (follow-ups)
- `apilink.py` (auto-link guide code spans to the C++ reference via the Doxygen
  tag file — we already emit `wowlib.tag`, just no consumer yet).
- A CI/`gh-pages` publish step; committed `uv.lock`-style pinning for docs deps.
