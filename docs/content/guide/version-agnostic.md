# Writing version-agnostic code

wowlib targets eleven client generations at once, and most tools want to work
on *whichever client the user opened* — not one hard-coded era. Every format
family therefore has two spellings:

- a **typed, era-resolved** one — `WMO<versions::wotlk>` in C++,
  `wmo.WMO.for_version(wowlib.Expansion.Wotlk)` in Python,
  `Formats.Wmo.WMO.Wotlk()` in C# — where every field is known at
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
      using namespace wowlib;
      if (fs.version() == versions::wotlk)
        dump_map<versions::wotlk>(fs);
      else if (fs.version() == versions::shadowlands)
        dump_map<versions::shadowlands>(fs);
    }
    ```

=== "Python"

    ```python
    import wowlib
    from wowlib.formats import wmo

    def open_model(fs: wowlib.fs.FileSystem, path: str) -> wmo.AnyWMO:
        """Load a WMO from WHATEVER client fs has open."""
        era = wowlib.expansion_of(fs.version)
        assert era is not None
        model = wmo.WMO.for_version(era)   # the era's concrete class
        model.read(fs, wowlib.FileKey(path))
        return model
    ```

=== "C#"

    ```csharp
    using wowlib;

    static Formats.Wmo.WMO OpenModel(Fs.FileSystem fs, string path)
    {
        // Load a WMO from WHATEVER client fs has open. The family base
        // carries the fs verbs, so no downcast is needed to read/write.
        var era = Global.ExpansionOf(fs.Version)
                  ?? throw new ArgumentException("unknown client era");
        var model = Formats.Wmo.WMO.ForVersion(era);
        model.Read(fs, new FileKey(path));
        return model;
    }
    ```

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
    static int TriangleCount(Formats.Wmo.WMO model) => model switch
    {
        // Era-gated data lives on the concretes; pattern matching narrows —
        // ranges with identical layouts share one class, so the arms stay few.
        Formats.Wmo.WMOVanillaToWotlk classic => Count(classic),
        Formats.Wmo.WMOShadowlands sl => Count(sl),
        _ => 0,
    };

    static int Count(Formats.Wmo.WMOVanillaToWotlk model)
    {
        var n = 0;
        foreach (var group in model.Groups)
            n += group.Body.Indices.Count / 3;
        return n;
    }
    ```

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
        using var table = Db.Table.Open(name, fs.Version);
        Audit(table);
    }
    ```
