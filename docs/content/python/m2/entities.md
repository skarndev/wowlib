# M2 entity

The user-facing compound `M2` — the assembly with everything baked in (the
MD20 body, the Legion+ chunks and every satellite file) — plus the other
file-level entities of the family: the external `Skin` LOD view, the shared
`Skeleton` and the `.bone` facial-pose file. Each family is documented as its version-agnostic
base plus a representative per-version class, shown generically as
`M2⟨version⟩` — the field set is identical across the versions a family
supports. Construct a concrete version with `for_version(expansion)`;
`read`/`write` speak the filesystem gateway.

The MD20 body (`M2Root`) and the Legion+ chunked shell (`M2ChunkedFile`) are the
assembly's two halves; their field-by-field references, with expansion and
FourCC badges, live on **[M2 root](root.md)** and **[M2 chunks](chunks.md)**.

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

::: wowlib.formats.m2.skin.SkinCataPlus
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

::: wowlib.formats.m2.SkeletonLegionPlus
    options:
      heading_level: 3
      show_root_toc_entry: true

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
