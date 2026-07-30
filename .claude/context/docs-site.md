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
`root-chunks`/`group-chunks` binary-struct pages remain mkdocstrings-generated.

### 2026-07-25 sweep (user's 6-point docs request)
- **M2 section = four pages in order**: `M2 entity` (entities.md — the compound
  M2 + Skin/Skeleton/BoneFile), `M2 root` (root.md), `M2 chunks` (chunks.md),
  `M2 records` (records.md). fields.md is GONE — a `Side` now carries its own
  `page` (falls back to `Format.fields_page`, which WMO still uses for both
  sides); forver/generic_pages/elem_links are keyed per page. The Any-union
  table moved to the M2 index.md.
- **Version badges are INCLUSIVE on both ends** (`_range_html`): an era-marker
  `until` (X.0.0) renders the PREVIOUS expansion (until WotLK → "Vanilla–TBC");
  a mid-expansion removal renders its own expansion as "< 8.3"; a range that
  collapses to one expansion is a single pill.
- **Element types are clickable**: `_vector_elements` keeps the CONCRETE
  element class (display goes ⟨version⟩-generic at rewrite); `_elem_pages`
  stores (page, module, anchor class) so `list[M2TextureTransform⟨version⟩]`
  links to the records page.
- **Records pages deduplicate versioned families** (`_records_markdown` behind
  per-StructPage `dedup_marker`s in records.md): one fully rendered
  latest-version representative per family + a "Version layouts" admonition
  listing each earlier era's added/dropped/retyped members (from the stubs).
  `StructPage.anchor_class` retargets element links of unrendered versions to
  the family rep. records.md joined M2's generic_pages.
- **Empty Type columns stripped** (`_strip_empty_type_columns`, on_post_page,
  all python/ pages): a docstring-section table whose Type cells are all blank
  loses the column (flag-enum Attributes tables).
- Every welded member now carries `[[=welder::doc]]` (2026-07-25 sweep of
  tracks/records/skin/skel/common types) — the stubs must be regenerated
  (`cmake --build build/bindings --target wowlib_pyi`) before doc builds pick
  docstring changes up.

### 2026-07-25 follow-up sweep
- **Records pages mirror wowdev's versioned-struct style** (`_records_markdown`
  v2): per family ONE merged member walk — heading + docstring from the
  latest-version rep (`members: false`), then each (member, layout) rendered
  once from the newest class declaring it, wearing an inclusive
  expansion-range badge when not family-wide (retyped members appear as
  adjacent per-era entries). Ranges derive from stub presence + class-range
  suffixes (`_suffix_span`); `_RECORD_BADGES` carries them from the markdown
  phase to `_augment_record_badges` (content phase). The old "Version layouts"
  admonition is gone.
- **`_retarget_class_refs`** (on_post_page, all python/ pages): unresolved
  autoref spans naming versioned classes that render nowhere (nanobind
  annotates properties with CONCRETE classes — M2.root -> M2RootLegionPlus)
  become links to the family's page, display generic. Targets come from
  elem_links stems (now also on the WMO config: WMORoot/WMOGroup/WMOGroupBody)
  and dedup pages' `anchor_class`. m2 `name_re` grew Exp2|Pabc|Psbc|Pgd1 so
  those payload names genericize too.
- **StringBlock + ChunkBlob documented on common.md** ("Chunk payload
  containers" section, module `wowlib.formats`) — blob/string annotations now
  autoref-resolve there.
- The M2 body field order on root.md is rebuilt the serializer's way
  (declaration order + `wire_after` splices — `_wire_order()` in the m2
  config parses the anchors; the wire_order array no longer exists).

### Binary int-width coverage (2026-07-25, follow-up)
Every fixed-width integer binary member now renders `Annotated[int, uintN]`
(nanobind erases the width to a bare `int`). Fixes that got it comprehensive:
- **`_struct_int_fields` is BRACE-AWARE**: each struct's region is brace-matched
  and its nested-struct bodies blanked, so a struct that CONTAINS one (SMOFog↦Fog,
  LightExtension↦Gradient) keeps the members after the nested type. `_blank_noncode`
  blanks strings + comments first so a `{`/`}` or an apostrophe in a doc/comment
  can't desync the brace counter (a lone `'` in a comment was eating whole struct
  decls). The member regex close is CONDITIONAL (`(?(vec)…|(?(arr)…))`) so
  `FBlock<std::uint16_t> x` is not misread as a uint16 field, and it handles
  `vector<array<uintN,K>>` (TXAC combos, skin bone quads → `list[list[Annotated…]]`).
  It also no longer lets a member's annotation `.*?` bleed across a PRECEDING
  excluded member's `]]` (which dragged `mark::exclude` onto the next field).
- **Nested vector depth** comes from the rendered signature, so one C++ struct
  (M2Track) covers both the pre-WotLK `list[…]` and WotLK+ `list[list[…]]` layouts.
- **`_annotate_int_widths` constrains the class group to the page's own classes**
  and forbids the heading body from crossing its `</hN>` — otherwise a module
  that is a namespace PREFIX of a sibling page (…m2 vs …m2.skin) or a heading not
  followed by a signature div (an enum/flag class) makes `.*?` swallow the region
  and starve the real attribute headings.
- **Template VALUE members** (M2TrackUInt16.values, FBlockUInt16.keys) are typed
  on the bare parameter `T`, so their width lives only in the welded class-name
  suffix — `_value_member_width` resolves it (`_VALUE_SUFFIX_WIDTH`).
- Entity side fields carry an `int_width` from `parse_members`/`_decl_int_width`
  (the fixed-width leaf of a scalar/vector/array/vector-of-array decl); the shell
  TXAC field annotates through it. `_apply_int_width` is the shared applier.
- Genuinely skipped: computed getter properties (SMODoodadDef.name_index) — not
  stored binary fields, so no size.
- The shared-primitive pages get width-only StructPages via a third config,
  `common_reference_config.py` (registered in FORMAT_MODULES + the reload shim):
  a MINIMAL Format (empty sides/generic_pages/forver, `name_re=r"(?!)"`) whose
  struct_pages are COMMON_TYPES (types.hpp → CArgb/C3iVector/fixed16 on
  common.md) and CORE_TYPES (client_version/file_key/error.hpp → ClientVersion/
  FileDataID/Error on core.md, module `wowlib`, filtered to those three so the
  giant top-level stub doesn't bloat cls_alt). The M2 entities page's Skeleton
  FDID lists + BoneFile ids/version are covered by ENTITY_SKELETON/ENTITY_BONE
  in the m2 config. Every engine pass but the int-width one no-ops for these.
- Genuinely bare (accepted): `size()`/`name_index()` getters (size_t/derived,
  not binary) and the NESTED `StringBlock.Entry.offset` (module `wowlib.formats`,
  a `Class.Nested.field` path the flat module.class.field matcher can't reach).
- `WMO` name_re is `WMO[A-Za-z]*?` not `+?`: the bare assembly class is
  `WMO`+version (WMOTheWarWithin) with no stem chars, so `+` never let the
  suffix genericize on the entity page.

### WMO chunk pages deduped (2026-07-25)
The root-chunks/group-chunks pages now dedup versioned binary structs like the M2
records page (dedup_marker on ROOT_CHUNKS/GROUP_CHUNKS; both pages joined
generic_pages). Key wrinkle vs M2 records: WMO binary structs have a welded
FAMILY BASE (bare `WMOBatch`/`WMOGroupHeader` beside the versioned
`WMOBatchLegionPlus` …), M2 records do not. So `_family_anchor(fam)` returns the
base (rank -1, already a generic name) when a family has one, else the latest
rep; `_records_markdown` renders a base-anchored family under a hand-written
`### Base⟨version⟩ {#module.Base}` heading + the base's docstring (the base has
no suffix to genericize), and walks ONLY the versioned classes for members (an
empty base would fabricate spurious badges). `StructPage.anchor_class` uses the
same `_family_anchor`, so every reference (the group entity's `batches` field,
the enum→struct backlinks in `_augment_chunks`, cross-page element links) lands
on `#module.WMOBatch`. Plain unversioned structs (SMOMaterial) and the flag
enums still render as full single listings.

### 2026-07-25 third batch: WMO page split + M2 value-template collapse
- **WMO now mirrors M2's page layout**: `entity.md` (the compound WMO class),
  `root.md` (renamed from fields.md, WMORoot only), `group.md` (WMOGroup +
  WMOGroupBody). Each Side carries `page=`; forver/generic_pages/elem_links
  keyed per page. See the WMO config.
- **Value-template record families collapse** (`_records_families`,
  `_emit_merged_members`, StructPage.value_templates): M2Track, FBlock,
  M2SplineKey, M2PartTrack are C++ templates over a value type T that welder
  mangles into ~25 class names. The records page documents each base ONCE under
  a hand-written `### Base⟨value⟩` markdown heading (id `module.Base`), its value
  member(s) shown as the generic `⟨value⟩` (`_genericize_value_members` replaces
  the sole leaf link in the value member's rendered signature, post-coercion);
  only the CANONICAL value variant (first in stub order) renders, the rest are
  dropped. The era (version) axis of M2Track still merges per-member with badges
  — `_emit_merged_members` derives the era span from the class RANGE suffix via
  split_range_suffix (NOT cls[len(stem):], which for a vt family holds the value
  part too).
- **Generic-type references** (`_render_value_template_refs`, on_post_page,
  BEFORE _retarget_class_refs and the xref genericization): a reference to any
  concrete instance (M2TrackSplineC3VectorWotlkPlus) rewrites to `Base[Value]`
  with both parts linked and recursion for nested templates
  (`M2Track[M2SplineKey[C3Vector]]`). Value suffix → display via the format's
  `value_alias` (CompQuat→M2CompQuat, Fixed16→fixed16, Spline*→M2SplineKey*,
  Float/UIntN→float/int). GOTCHA: the rewriter is format-agnostic —
  `_vt_registry`/`_vt_value_alias` merge ALL formats — because it ran per-format
  and the WMO pass (empty value_alias) was consuming M2 refs before M2's pass.

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

### ADT docs (2026-07-26)
ADT is the exception to the FourCC field-badge engine: its `ADT<V>`/`MapChunk<V>`
members carry NO `=chunk()`/`=since()` annotations (sub-chunks are hand-serialized,
version-gating is by conditional trait bases), so the entity + cell pages are plain
mkdocstrings `:::` directives on a REPRESENTATIVE concrete class (ADTBfaPlus,
MapChunkCataPlus — the fullest layouts) with a prose note on version-gating, NOT
generated field tables. `adt_reference_config.py` is a minimal `common`-style
Format (empty sides, `name_re=(?!)`) with WIDTH-ONLY StructPages: CHUNKS
(adt/chunks.md, the binary structs) + LIQUID (adt/entity.md, the four structured
liquid records, names_filter'd). Registered in FORMAT_MODULES + the reload shim +
mkdocs nav (python/adt/{index,entity,cells,chunks}.md). Vendored
adt_wowdev_anchors.json (extracted from the wiki HTML, HTML-entities decoded).
Guide: content/guide/maps.md gained an "Editing terrain (ADT)" section with the
version-agnostic add-a-texture walkthrough. Build is clean.

## Not yet ported from welder (follow-ups)
- `apilink.py` (auto-link guide code spans to the C++ reference via the Doxygen
  tag file — we already emit `wowlib.tag`, just no consumer yet).
- A CI/`gh-pages` publish step; committed `uv.lock`-style pinning for docs deps.
