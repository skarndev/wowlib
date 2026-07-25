# M2 root — the MD20 body

The model payload — the client's own `M2Root`. Unlike the chunked formats it is
**offset-addressed**: fields carry no FourCC; their wire positions come from the
entity's canonical `wire_order`, which is also the order they are listed in
below. Pre-Legion this *is* the `.m2` file; Legion+ it is the content of the
[chunked shell](chunks.md)'s `MD21` chunk.

Rather than repeat eleven near-identical class listings, this page documents the
**generic model**: `M2Root` is the abstract base; the per-version layout below
is shown generically as `M2Root⟨version⟩`. A field whose availability is
**version-restricted** carries an expansion badge naming the range of clients
that have it (both ends inclusive); a field with **no badge exists in every
version**. The badges are generated from the C++ sources, so they cannot drift.

<!-- m2-legend -->

Two wire fields are managed for you and hidden from Python: the leading `MD20`
magic, and WotLK+'s `num_skin_profiles` (stamped from the assembly's
`skins` vector on write).

## The M2Root base

::: wowlib.formats.m2.root.M2Root
    options:
      heading_level: 3
      show_root_toc_entry: true

## Fields

<!-- m2-root-fields -->

## Global flags

::: wowlib.formats.m2.root.GlobalFlags
    options:
      heading_level: 3
      show_root_toc_entry: true
