# M2 records

The decoded data records an M2 body is made of — animation sequences and
tracks (both timeline eras), bones, colors, textures and their transforms,
attachments, events, lights, cameras, ribbon and particle emitters, the
skin-profile tables and the `.skel` chunk payloads. Records are concrete
per-version classes (`M2CompBoneWotlk`, `M2SequenceCata`, …): construct the
one matching your model's version directly. Wire integer fields show their
on-disk width (`Annotated[int, uint16]`), generated from the C++ sources.

## Body records

The records the MD20 body's vectors are made of, from
`wowlib.formats.m2.body.records`: tracks and timestamps, sequences, bones,
materials, scene objects (attachments, events, lights, cameras), the emitters
and the chunked-shell payload records (`AnimFileEntry`, `Exp2Data`, …).

::: wowlib.formats.m2.body.records
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 3

## Skin records

The `.skin` LOD-view tables, from `wowlib.formats.m2.skin` (the [Skin
entity](entities.md) itself is documented with the other entities): the
skin profile, its submesh sections and the render/shadow batches.

::: wowlib.formats.m2.skin
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 3
      filters:
        - "!^Skin"
        - "!^AnySkin$"

## Skeleton chunk payloads

The `.skel` SK*1 chunk payload records, from `wowlib.formats.m2` (the
[Skeleton entity](entities.md) is documented with the other entities): the
identity header, the sequence/bone/attachment blocks and the parent link.

::: wowlib.formats.m2
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 3
      # Inclusive filter: keep only the Skel* payload records ([A-Z] so the
      # Skeleton entity family itself stays on the entities page).
      filters:
        - "^Skel[A-Z]"
