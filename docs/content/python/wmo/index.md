# WMO format

The **WMO** (World Model Object) format, modelled as versioned entities. `WMO` is
the common base; each expansion has a concrete subclass (`WMOWotlk`,
`WMOShadowlands`, …) exposing that version's chunk layout. The `root` and `group`
submodules hold the typed chunk structs.

- **[Fields & versions](fields.md)** — the **generic WMO**: every root- and
  group-file field, each tagged with the expansion range it is available in
  (generated from the sources, so the ranges never drift).
- **[Root chunks](root-chunks.md)** — the wire structs used by the root file.
- **[Group chunks](group-chunks.md)** — the wire structs used by a group file.

!!! info "One layout per expansion, documented generically"
    The WMO layout is version-parametric — `WMO<version>` (Python: `WMOWotlk`,
    `WMOShadowlands`, …) per expansion. Every version shares the same field
    *names*; a field simply exists only within its expansion range. The
    [Fields & versions](fields.md) page documents that generically rather than
    repeating a dozen per-version class listings.

See the guide's **[Reading a WMO](../../guide/wmo.md)** for a worked example.
