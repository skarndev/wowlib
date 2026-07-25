# M2 fields & versions

An M2 model body is version-parametric: the layout differs by client version,
which wowlib models as one `M2Root<version>` per expansion (Python:
`M2RootWotlk`, `M2RootShadowlands`, …). Rather than repeat eleven near-identical
class listings, this page documents the **generic model** — the version-agnostic
base plus the per-version layout, shown generically as `M2Root⟨version⟩` /
`M2ChunkedFile⟨version⟩`. The field set is identical across every expansion; only which
fields are *active* varies, shown by the badges.

A field whose availability is **version-restricted** carries an expansion badge;
a field with **no badge exists in every version its family supports** (all
clients for the body; Legion+ for the chunked shell). The badges are generated
from the C++ sources, so they cannot drift.

<!-- m2-legend -->

## Version-agnostic unions

Each family exposes an `Any…` **type alias** — the union of all its per-version
classes, bound as a real `types.UnionType` on its module (importable, and usable
in `isinstance` on Python ≥ 3.10). It is the runtime return type of
`for_version(expansion: Expansion)` and the natural annotation when your code
handles any version:

| Alias | Module | Union of |
|---|---|---|
| `AnyM2` | `wowlib.formats.m2` | every `M2⟨version⟩` |
| `AnyM2Root` | `wowlib.formats.m2.root` | every `M2Root⟨version⟩` |
| `AnyM2ChunkedFile` | `wowlib.formats.m2.root` | every Legion+ `M2ChunkedFile⟨version⟩` |
| `AnySkin` | `wowlib.formats.m2.skin` | every WotLK+ `Skin⟨version⟩` |
| `AnySkeleton` | `wowlib.formats.m2` | every Legion+ `Skeleton⟨version⟩` |

## MD20 body

The model payload — the client's own `M2Root`. Unlike the chunked formats it is
**offset-addressed**: fields carry no FourCC; their wire positions come from the
entity's canonical `wire_order`, which is also the order they are listed in
below. Pre-Legion this *is* the `.m2` file; Legion+ it is the content of the
shell's `MD21` chunk. `M2Root` is the abstract base; the per-version layout
below is shown generically as `M2Root⟨version⟩`.

Two wire fields are managed for you and hidden from Python: the leading `MD20`
magic, and WotLK+'s `num_skin_profiles` (stamped from the assembly's
`skins` vector on write).

::: wowlib.formats.m2.root.M2Root
    options:
      heading_level: 3
      show_root_toc_entry: true

<!-- m2-root-fields -->

### Global flags

::: wowlib.formats.m2.root.GlobalFlags
    options:
      heading_level: 4
      show_root_toc_entry: true

## Legion+ chunked shell

From Legion (7.0.1) on, the on-disk `.m2` is a chunked stream: the MD20 image
moves into the `MD21` chunk, joined by satellite chunks — FileDataID references,
extended particle data, parent-model overrides, inline physics. Chunk ids are
**not** reversed on disk, unlike every other WoW chunk format. Each field below
carries its chunk **FourCC** (linking to wowdev.wiki); an untouched shell
rewrites byte-for-byte.

::: wowlib.formats.m2.chunked.M2ChunkedFile
    options:
      heading_level: 3
      show_root_toc_entry: true

<!-- m2-shell-fields -->
