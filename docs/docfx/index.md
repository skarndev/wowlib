# wowlib C# API reference

The complete API of the **Wowlib** NuGet package — the generated managed
wrapper over the native library, documented from the same `[[=welder::doc]]`
annotations the Python and C++ references render.

**[Browse the `wowlib` namespace →](api/wowlib.html)**

Highlights:

- [`wowlib.Fs`](api/wowlib.Fs.html) — the client filesystem gateway (MPQ & CASC).
- [`wowlib.Formats.*`](api/wowlib.Formats.html) — WMO, M2, ADT, WDT, WDL, BLP.
  Every format family has a version-agnostic base (`ForVersion`, the
  synthesized family surface) beside its per-era range classes.
- [`wowlib.Db`](api/wowlib.Db.html) — the generic ClientDB `Table` engine.
  Of the typed per-table facades, only the representative **Map** family is
  listed here — every other table follows the identical pattern; see the
  [database guide](../guide/db/) for the walkthrough.

Prefer task-oriented reading? Start from the [guide](../) — every example
there carries a C# tab.
