# C# API Reference

The full **C# reference** is a standalone DocFX site — generated from the
`WoWLib` NuGet package's managed surface (the welder-generated wrapper, the
per-era format factories and the typed db facades), carrying the same doc text
as the Python and C++ references: every `[[=welder::doc]]` annotation becomes
the member's `<summary>`.

<a class="md-button md-button--primary" href="../api-cs/index.html">Open the C# reference →</a>

It opens in this same site under `/api-cs`. Two things to know:

- **The typed db facades are filtered to the representative `Map` family** —
  one class + one row struct per (table × era) would be thousands of
  near-identical types. Every other table follows the identical pattern; the
  [database guide](guide/db.md) walks through it.
- **Family bases carry the version-agnostic surface.** A `ForVersion(...)`
  result's members (the synthesized family surface) are documented on the base
  class pages — `Formats.WMO.WMO`, `Formats.ADT.ADT`, … — beside the per-era
  range classes.

If the link 404s, the reference has not been generated yet — build the C#
surface once, then the site:

```bash
cmake --preset gcc16-csharp
cmake --build build/csharp --target wowlib_cs_sources   # no shim compile
.venv/bin/python docs/build.py build
```

`wowlib_cs_sources` produces only the generated C# (wrapper + facades); DocFX
runs a design-time Roslyn build over the generated `WoWLib.csproj`, so the
native library is never required. `docs/build.py docfx` rebuilds just this
reference.
