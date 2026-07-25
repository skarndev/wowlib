# M2 records

The decoded data records an M2 body is made of — animation sequences and
tracks (both timeline eras), bones, colors, textures and their transforms,
attachments, events, lights, cameras, ribbon and particle emitters, the
skin-profile tables and the `.skel` chunk payloads. Records are concrete
per-version classes (`M2CompBoneWotlk`, `M2SequenceCata`, …): construct the
one matching your model's version directly.

To keep the reference readable, each record **family documents once**, the way
wowdev.wiki lists a versioned struct: one merged member walk under the generic
`⟨version⟩` name. A member carries an **expansion-range badge** when it does
not exist across the family's whole range; a member whose layout changed
appears once per era, each entry badged with the clients it covers — a
badge-less member is identical in every version. Wire integer fields show
their on-disk width (`Annotated[int, uint16]`), generated from the C++
sources.

The animation-track types — **`M2Track`**, **`FBlock`**, **`M2SplineKey`** and
**`M2PartTrack`** — are templates over a *value type*, so they document once as
`Base⟨value⟩`, their value member shown generically. A record that uses one
refers to it as `M2Track[C4Quaternion]`, both parts linking through to their
own documentation.

## Body records

The records the MD20 body's vectors are made of, from
`wowlib.formats.m2.root.record`: tracks and timestamps, sequences, bones,
materials, scene objects (attachments, events, lights, cameras) and the
emitters.

<!-- m2-records-body -->

## Chunked-shell records

The Legion+ companion-chunk payload records, from
`wowlib.formats.m2.chunked.record`: the AFID entries, the extended particle
parameters (`EXPT`/`EXP2`), the per-light `DETL` overrides and the
parent-model override payloads.

<!-- m2-records-chunked -->

## Skin records

The `.skin` LOD-view tables, from `wowlib.formats.m2.skin` (the [Skin
entity](entities.md) itself is documented with the other entities): the
skin profile, its submesh sections and the render/shadow batches.

<!-- m2-records-skin -->

## Skeleton chunk payloads

The `.skel` SK*1 chunk payload records, from `wowlib.formats.m2` (the
[Skeleton entity](entities.md) is documented with the other entities): the
identity header, the sequence/bone/attachment blocks and the parent link.

<!-- m2-records-skel -->
