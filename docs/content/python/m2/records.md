# M2 records

The decoded data records an M2 body is made of — animation sequences and
tracks (both timeline eras), bones, colors, textures and their transforms,
attachments, events, lights, cameras, ribbon and particle emitters, the
skin-profile tables and the `.skel` chunk payloads. Records are concrete
per-version classes (`M2CompBoneWotlk`, `M2SequenceCata`, …): construct the
one matching your model's version directly.

::: wowlib.formats.m2.records
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 2
