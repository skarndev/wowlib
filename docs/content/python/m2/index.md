# M2 format

The **M2** model format (doodads, creatures, characters, spell effects),
modelled as versioned entities across every targeted client — vanilla's
monolithic MD20 through the Legion+ chunked shell. `M2` is the common base;
each expansion has a concrete subclass (`M2Wotlk`, `M2Shadowlands`, …)
exposing that version's layout. Satellite files bake into the entity on
`read()` and split back out on `write()`:

- **`.skin` LOD views** (WotLK+; embedded in the body before that) land in
  `skins` / `lod_skins`.
- **`.anim` external sequences** — a sequence with `(flags & 0x130) == 0`
  keeps its track data in a per-sequence file; reading merges it into the
  tracks, writing re-splits it from the sequence flags.
- **`.skel` shared skeletons** (Legion 7.3+) are first-class: `Skeleton`
  reads/writes standalone (they are shared between models via the parent
  link), and a skel-based model carries one fully decoded on `skel`.
- **`.bone` facial poses** and the opaque **`.phys`** physics blob ride
  along on the Legion+ assembly.

Pages:

- **[Fields & versions](fields.md)** — the **generic model**: every MD20 body
  field and every Legion+ shell chunk, each tagged with the expansion range it
  is available in (generated from the sources, so the ranges never drift).
- **[Entities](entities.md)** — `M2`, `M2Data`, `Skin`, `M2File`,
  `Skeleton`, `BoneFile` and their per-version classes.
- **[Records](records.md)** — the decoded data records (sequences, bones,
  animation tracks, textures, cameras, emitters, skin tables).

!!! info "Offset format: canonical writes, semantic round-trip"
    Unlike the chunked formats, M2's wire layout is offset-addressed, and
    wowlib re-lays it out canonically on every write. The guarantee is
    *semantic*: a written model re-reads equal (the chunked Legion+ shell
    itself still rewrites byte-for-byte while untouched).

!!! warning "Version-gated fields do not exist off-era"
    A version's class carries **only** the fields that client defines —
    `M2DataVanilla` has `skin_profiles` (embedded views), which WotLK+
    bodies drop in favour of external `.skin` files on the assembly's
    `skins`; touching an off-era field raises `AttributeError` instead of
    silently writing nothing. (Derived wire counters like the body's skin
    count are stamped on write and hidden from Python entirely.)
