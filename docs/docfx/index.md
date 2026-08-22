# wowlib C# API reference

The complete API of the **WoWLib** NuGet package — the generated managed
wrapper over the native library, documented from the same `[[=welder::doc]]`
annotations the Python and C++ references render.

**[Browse the `WoWLib` namespace →](api/WoWLib.html)**

Highlights:

- [`WoWLib.Fs`](api/WoWLib.Fs.html) — the client filesystem gateway (MPQ & CASC).
- [`WoWLib.Formats.*`](api/WoWLib.Formats.html) — WMO, M2, ADT, WDT, WDL, BLP.
  Every format family has a version-agnostic base (`ForVersion`, the
  synthesized family surface) beside its per-era range classes.
- [`WoWLib.Db`](api/WoWLib.Db.html) — the generic ClientDB `Table` engine.
  Of the typed per-table facades, only the representative **Map** family is
  listed here — every other table follows the identical pattern; see the
  [database guide](../guide/db/) for the walkthrough.

Prefer task-oriented reading? Start from the [guide](../) — every example
there carries a C# tab.
