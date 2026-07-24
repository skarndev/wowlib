# M2 entities

The file-level entities of the M2 family: the `M2` assembly (body + baked
satellites), the external `Skin` LOD view, the shared `Skeleton` and the
`.bone` facial-pose file. Each family is documented as its version-agnostic
base plus a representative per-version class, shown generically as
`M2⟨version⟩` — the field set is identical across the versions a family
supports. Construct a concrete version with `for_version(expansion)`;
`read`/`write` speak the filesystem gateway.

The MD20 body (`M2Data`) and the Legion+ chunked shell (`M2File`) are the
assembly's two halves; their field-by-field reference, with expansion and
FourCC badges, lives on **[Fields & versions](fields.md)**.

## The M2 assembly

::: wowlib.formats.m2.M2
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.m2.M2TheWarWithin
    options:
      heading_level: 3
      show_root_toc_entry: true

## Skin — the .skin LOD view

The external skin file (WotLK+; embedded in the body before that): submeshes,
render batches and the index/triangle tables of one LOD view.

::: wowlib.formats.m2.skin.Skin
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.m2.skin.SkinTheWarWithin
    options:
      heading_level: 3
      show_root_toc_entry: true

## Skeleton — the shared .skel

The shared model skeleton (Legion 7.3+): bones, attachments and sequences a
skel-based model moved out of its MD20 image, shareable between models via the
parent link. The SK*1 chunk payload records (`SkelHeader`, `SkelBones`, …) are
documented with the other [records](records.md).

::: wowlib.formats.m2.Skeleton
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.m2.SkeletonTheWarWithin
    options:
      heading_level: 3
      show_root_toc_entry: true

## Body and shell classes

The per-version body and shell classes render here for completeness; their
fields are documented on [Fields & versions](fields.md).

::: wowlib.formats.m2.body.M2DataTheWarWithin
    options:
      heading_level: 3
      show_root_toc_entry: true
      members: false

::: wowlib.formats.m2.body.M2FileTheWarWithin
    options:
      heading_level: 3
      show_root_toc_entry: true
      members: false

## BoneFile — the .bone facial poses

One `.bone` file per FacePose (808) sequence variant (WoD+), referenced by
BFID: which bones get facial-pose offset matrices, and the matrices. The layout
is stable across every client that ships them, so the entity is not
version-templated.

::: wowlib.formats.m2.bone.BoneFile
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.m2.bone.BoneFilePrelude
    options:
      heading_level: 3
      show_root_toc_entry: true
