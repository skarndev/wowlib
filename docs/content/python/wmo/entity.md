# WMO entity

The user-facing compound **`WMO`** — the assembly with everything baked in: the
root file plus every group file, read and written as one object. `WMO` is the
abstract base; each expansion has a concrete subclass (`WMOWotlk`,
`WMOShadowlands`, …) exposing that version's layout. Construct a concrete
version with `for_version(expansion)`; `read`/`write` speak the filesystem
gateway (or plain buffers, with the group files passed alongside).

The root file (`WMORoot`) and the group files (`WMOGroup`) are the assembly's
two halves; their field-by-field references, with expansion and FourCC badges,
live on **[WMO root](root.md)** and **[WMO group](group.md)**.

## The WMO assembly

::: wowlib.formats.wmo.WMO
    options:
      heading_level: 3
      show_root_toc_entry: true

::: wowlib.formats.wmo.WMOTheWarWithin
    options:
      heading_level: 3
      show_root_toc_entry: true

!!! info "One layout per expansion, documented generically"
    The WMO layout is version-parametric — `WMO<version>` (Python: `WMOWotlk`,
    `WMOShadowlands`, …) per expansion. Every version shares the same field
    *names*; a field simply exists only within its expansion range. The
    [WMO root](root.md) and [WMO group](group.md) pages document that
    generically rather than repeating a dozen per-version class listings.

See the guide's **[Reading a WMO](../../guide/wmo.md)** for a worked example.
