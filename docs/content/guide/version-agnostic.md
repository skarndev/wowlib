# Writing version-agnostic code

wowlib targets eleven client generations at once, and most tools want to work
on *whichever client the user opened* — not one hard-coded era. Every format
family therefore has two spellings:

- a **typed, era-resolved** one — `WMO<versions::wotlk>` in C++,
  `wmo.WMO.for_version(wowlib.Expansion.Wotlk)` in Python,
  `Formats.WMO.WMO.Era.Wotlk()` in C# — where every field is known at
  compile/checking time;
- a **dynamic** one — the same factory driven by a runtime era — returning
  something you can still read, write, convert and inspect without knowing
  the era in advance.

This page is about the second spelling.

## From an opened client to the right entity

The filesystem knows which client it opened (`fs.version`), and the eras form
a ladder (`Expansion`), so generic code derives the era instead of asking for
it:

=== "C++"

    ```cpp
    // C++ is statically polymorphic — the generic spelling is a template,
    // and a runtime era is dispatched onto it once, at the boundary:
    template <wowlib::ClientVersion V>
    void dump_map(wowlib::fs::FileSystem& fs)
    {
      wowlib::formats::wdt::WDT<V> world;
      if (auto r = world.read(fs, wowlib::FileKey{
            "World/Maps/Azeroth/Azeroth.wdt"}); !r)
        return report(r.error());
      // Version-gated members cost nothing to gate on:
      if constexpr (V >= wowlib::versions::wod)
        use(world.lights);
    }

    void dump_any(wowlib::fs::FileSystem& fs)
    {
      // fs.version() remembers what was opened; dispatch it once onto the
      // template — per era you support, exactly what the binding facades do.
      // Compare on format_lineage(), never on the raw version: that is what
      // puts a Classic client on the engine it actually is.
      using namespace wowlib;
      const ClientVersion engine = fs.version().format_lineage();
      if (engine == versions::wotlk)
        dump_map<versions::wotlk>(fs);
      else if (engine == versions::shadowlands)
        dump_map<versions::shadowlands>(fs);
    }
    ```

=== "Python"

    ```python
    import wowlib
    from wowlib.formats import wmo

    def open_model(fs: wowlib.fs.FileSystem, path: str) -> wmo.AnyWMO:
        """Load a WMO from WHATEVER client fs has open."""
        model = wmo.WMO.for_version(fs.version)   # the version's concrete class
        model.read(fs, wowlib.FileKey(path))
        return model
    ```

    `for_version` takes either an `Expansion` (when you mean "the WotLK
    layout") or a full `ClientVersion` (when you mean "whatever this client
    is"). Prefer the version: it is the only form that handles Classic
    clients, whose expansion number says nothing about their file formats —
    see [Classic clients](#classic-clients) below.

=== "C#"

    ```csharp
    using WoWLib;

    static Formats.WMO.WMO OpenModel(Filesystem.FileSystem fs, string path)
    {
        // Load a WMO from WHATEVER client fs has open. The family base
        // carries the members every era binds identically — the fs verbs
        // AND the data — so no downcast is needed to read, write or process.
        var model = Formats.WMO.WMO.ForVersion(fs.Version);
        model.Read(fs, new FileKey(path));
        return model;
    }
    ```

    `ForVersion` is overloaded on `Expansion` and on `ClientVersion`. Prefer
    the version: it is the only form that handles Classic clients — see
    [Classic clients](#classic-clients) below.

## Classic clients

WoW Classic Era, the Classic progression realms (BCC, WotLK, Cataclysm, MoP
Classic) and the Anniversary realms are **not** old clients. They are the
modern client, rebuilt from whatever retail branch was current at the time,
shipping content that *looks* like an old expansion. So:

- Classic Era 1.15.9 is a Midnight-era client. Its models are chunked MD21
  M2s, its maps are split ADTs behind a `MAID` WDT, its tables are WDC DB2s,
  and its storage is CASC — none of which existed in 1.12.
- Cataclysm Classic 4.4.2 writes War Within-era files, not Cataclysm ones.
- One version *number* can span two engines: Cata Classic 4.4.0 shipped on
  the Dragonflight branch in April 2024 and on The War Within branch by
  October, under the same `4.4.0`.
- The `wow_classic_titan` product calls itself `3.80.2`, a version no
  expansion ever had.

The version tuple is therefore useless as a format key, and the **build
number** is authoritative — Blizzard's build counter is global across every
product, so a build uniquely places a client on the engine timeline.

wowlib models this with a **flavor** on `ClientVersion` and a
`format_lineage` derived from it:

=== "C++"

    ```cpp
    using namespace wowlib;

    constexpr ClientVersion cata_classic{4, 4, 2, 60895, ClientFlavor::Classic};

    static_assert(cata_classic.format_lineage() == versions::tww);
    static_assert(cata_classic.storage_kind() == StorageKind::Casc);
    static_assert(expansion_of(cata_classic) == Expansion::Cata);  // content
    // ... and so the entity IS the War Within one, not the Cataclysm one:
    static_assert(std::is_same_v<formats::wmo::WMO<cata_classic>,
                                 formats::wmo::WMO<versions::tww>>);
    ```

=== "Python"

    ```python
    from wowlib import versions, ClientFlavor, ClientVersion

    cata_classic = versions.classic_cata          # 4.4.2.60895 (Classic)
    cata_classic.format_lineage                   # 11.2.7.65299 — the engine
    cata_classic.storage_kind                     # StorageKind.Casc
    wowlib.expansion_of(cata_classic)             # Expansion.Cata — the content

    # for_version places it by build; no separate Classic class exists.
    model = wmo.WMO.for_version(cata_classic)     # -> WMOTheWarWithin

    # Any build, named exactly:
    v = ClientVersion(4, 4, 0, 54481, ClientFlavor.Classic)
    wmo.WMO.for_version(v)                        # -> WMODragonflight
    ```

=== "C#"

    ```csharp
    var cataClassic = Versions.Global.ClassicCata;   // 4.4.2.60895 (Classic)
    var engine = cataClassic.FormatLineage;          // 11.2.7.65299
    var model = Formats.WMO.WMO.ForVersion(cataClassic);  // -> WMOTheWarWithin
    ```

Because a Classic version resolves onto an *existing* retail range class,
supporting Classic adds no new types, no new instantiations and no binary
size — a Classic client simply *is* its engine's client.

`wowlib.versions` carries a constant per living Classic line
(`classic_era`, `classic_bcc`, `classic_wotlk`, `classic_cata`,
`classic_mop`, `anniversary`), but those products ship new builds
continuously. For anything real, read the version off the installation
instead — see
[Reading client files](client-files.md#detecting-what-is-installed).

## Processing without naming an era

Members shared by every era are usable straight off the dynamic result — the
type systems keep you honest about the rest:

=== "C++"

    ```cpp
    // Inside the template, everything is plain typed code; per-era members
    // sit behind `if constexpr` and cost nothing on other eras.
    template <wowlib::ClientVersion V>
    std::size_t triangle_count(const wowlib::formats::wmo::WMO<V>& model)
    {
      std::size_t n = 0;
      for (const auto& group : model.groups)
        n += group.body.indices.size() / 3;
      return n;
    }
    ```

=== "Python"

    ```python
    def triangle_count(model: wmo.AnyWMO) -> int:
        """AnyWMO = the union of every era's class: attribute access through
        it is checked against ALL members, so this function provably works
        on every era."""
        return sum(len(group.body.indices) // 3 for group in model.groups)

    def lightmapped(model: wmo.AnyWMO) -> bool:
        # Era-gated data: narrow to the family classes that carry it.
        if isinstance(model, (wmo.WMOShadowlands, wmo.WMODragonflight)):
            ...   # model's era-specific members type-check inside the branch
        return False
    ```

    `AnyWMO` is a real importable union (`types.UnionType`), so it works in
    `isinstance` too; the family base `wmo.WMO` is a real common base class,
    so `isinstance(model, wmo.WMO)` accepts every era.

=== "C#"

    ```csharp
    static int TriangleCount(Formats.WMO.WMO model)
    {
        // The family base carries every member the eras bind identically —
        // the C# twin of Python's AnyWMO intersection. Members typed per era
        // surface as their own family base (Groups is a live view of WMOGroup,
        // group.Body is a WMOGroupBody), so the whole tree stays navigable
        // without ever naming an era.
        var n = 0;
        foreach (var group in model.Groups)
            n += group.Body.Indices.Count / 3;
        return n;
    }

    static bool Lightmapped(Formats.WMO.WMO model) => model switch
    {
        // Era-gated data lives on the concretes; pattern matching narrows —
        // ranges with identical layouts share one class, so the arms stay few.
        Formats.WMO.WMOShadowlands sl => UsesLightmaps(sl),
        Formats.WMO.WMODragonflight df => UsesLightmaps(df),
        _ => false,
    };
    ```

## One root above the families (C#)

Every file-level entity in C# — the six format family bases and `BLP` —
derives **`Formats.FileEntity`**, which carries the contract they all share
(`Validate`, `EnsureValid`). A mixed batch of loaded files processes
uniformly, no format named:

```csharp
using WoWLib;

static void AuditAll(IEnumerable<Formats.FileEntity> entities)
{
    foreach (var e in entities)
    {
        using var report = e.Validate();   // dispatches format, then version
        // ...
    }
}
```

Reading and writing stay on the family bases (`Formats.WMO.WMO.Read(...)`,
…): ADT's verbs genuinely take an extra alpha-format argument, so a uniform
root signature would be a lie. Copies are the C# copy-constructor idiom —
`new WMOWotlk(model)` — and `Dispose` is optional deterministic cleanup (the
finalizer releases native memory on collection anyway; `using` just makes it
prompt).

## Converting between eras

`convert` is the bridge from generic reading to targeted writing — read from
one client, retarget, write to another:

=== "C++"

    ```cpp
    #include <wowlib/formats/wmo/convert.hpp>

    auto retail = wowlib::formats::convert<wowlib::versions::shadowlands>(model);
    if (retail)
      retail->write(fs_927, wowlib::FileKey{"world/wmo/ported.wmo"});
    ```

=== "Python"

    ```python
    retail = model.convert(wowlib.Expansion.Shadowlands)
    retail.write(fs_927, wowlib.FileKey("world/wmo/ported.wmo"))
    ```

=== "C#"

    ```csharp
    // Conversion is era-typed in C# today; read at the source era, then
    // construct the target era and carry the data over, or round-trip
    // through the typed classes. (A base-level Convert is planned.)
    ```

## The database is version-agnostic by construction

The generic `Table` engine takes any table name and any client version — and
even the typed per-era surfaces are just era-resolved views of it. Generic
tooling loops are one call away:

=== "C++"

    ```cpp
    for (std::string_view name : {"Map", "AreaTable", "Spell"})
      if (auto table = wowlib::db::DynTable::open(name, fs.version()))
        audit(*table);
    ```

=== "Python"

    ```python
    for name in wowlib.db.table_names(fs.version):
        table = wowlib.db.Table.open(name, fs.version)
        audit(table)
    ```

=== "C#"

    ```csharp
    foreach (var name in new[] { "Map", "AreaTable", "Spell" })
    {
        using var table = Database.Table.Open(name, fs.Version);
        Audit(table);
    }
    ```
