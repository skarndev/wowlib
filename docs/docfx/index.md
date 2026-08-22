# wowlib C# API reference

The complete API of the **WoWLib** NuGet package — the generated managed
wrapper over the native library, documented from the same `[[=welder::doc]]`
annotations the Python and C++ references render.

**[Browse the `WoWLib` namespace →](api/WoWLib.html)**

Highlights:

- [`WoWLib.Filesystem`](api/WoWLib.Filesystem.html) — the client filesystem gateway (MPQ & CASC).
- [`WoWLib.Formats.*`](api/WoWLib.Formats.html) — WMO, M2, ADT, WDT, WDL, BLP.
  Every format family has a version-agnostic base (`ForVersion`, the
  synthesized family surface) beside its per-era range classes.
- [`WoWLib.Database`](api/WoWLib.Database.html) — the generic ClientDB `Table`
  engine. The typed per-table facades in `WoWLib.Database.Tables` are **not
  listed individually**: they are generated from the community
  [WoWDBDefs](https://github.com/wowdev/WoWDBDefs) definitions, one opener
  class plus one row struct per (table × game version), named per C#
  conventions — `Map` becomes `MapWotlk` with `MapWotlkRow`, `SpellVisualKitModelAttach`
  becomes `SpellVisualKitModelAttachWotlk`, and so on. Every table follows the
  identical shape; the [database guide](../guide/db/) walks through it once.

Prefer task-oriented reading? Start from the [guide](../) — every example
there carries a C# tab.
