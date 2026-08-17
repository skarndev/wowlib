# Reading client files (MPQ & CASC)

Every WoW client ships its data in an archive storage: **MPQ** archives with
their patch chains for pre-Warlords clients (< 6.0), **CASC** storage for
everything newer. wowlib wraps both (StormLib and CascLib) behind one gateway —
`FileSystem` — so the code that *uses* files never branches on the storage
technology. You say which client version you are opening; the version decides
the backend, the archive/patch chain order, and how files are addressed.

Files are identified by a `FileKey` — a path, a **FileDataID** (the numeric
identity CASC clients use), or both. On an MPQ client paths address everything;
on a CASC client the FileDataID does, and a **listfile** (the community
`fileDataId;filepath` CSV) lets you keep using paths there too.

## Opening a client

=== "C++"

    ```cpp
    #include <wowlib/fs/filesystem.hpp>

    // Wrath of the Lich King 3.3.5a — an MPQ-era client.
    auto fs = wowlib::fs::FileSystem::open({
      .client_path = "/games/World of Warcraft 3.3.5a",
      .version = wowlib::versions::wotlk,
    });
    if (!fs)
      return report(fs.error());     // Result<T> everywhere — no exceptions

    // Shadowlands 9.2.7 — a CASC client; the listfile enables path lookups.
    auto retail = wowlib::fs::FileSystem::open({
      .client_path = "/games/World of Warcraft 9.2.7",
      .version = wowlib::versions::shadowlands,
      .listfile_csv = "/data/listfile.csv",
    });
    ```

=== "Python"

    ```python
    import wowlib

    # Wrath of the Lich King 3.3.5a — an MPQ-era client.
    fs = wowlib.fs.FileSystem.open(wowlib.fs.FileSystemSettings(
        client_path="/games/World of Warcraft 3.3.5a",
        version=wowlib.versions.wotlk))

    # Shadowlands 9.2.7 — a CASC client; the listfile enables path lookups.
    retail = wowlib.fs.FileSystem.open(wowlib.fs.FileSystemSettings(
        client_path="/games/World of Warcraft 9.2.7",
        version=wowlib.versions.shadowlands,
        listfile_csv="/data/listfile.csv"))

    # FileSystem is a context manager if you prefer scoped lifetime:
    with wowlib.fs.FileSystem.open(wowlib.fs.FileSystemSettings(
            client_path="/games/World of Warcraft 3.3.5a",
            version=wowlib.versions.wotlk)) as scoped:
        ...
    ```

=== "C#"

    ```csharp
    using wowlib;
    using Fs = wowlib.Fs;
    using Versions = wowlib.Versions;

    // Wrath of the Lich King 3.3.5a — an MPQ-era client.
    using var settings = new Fs.FileSystemSettings(
        clientPath: "/games/World of Warcraft 3.3.5a",
        version: Versions.Global.Wotlk,
        locale: Locale.enUS,
        projectDirectory: null,
        listfileCsv: null,
        customFdidStart: new FileDataId(),
        cascProduct: "wow");
    using var fs = Fs.FileSystem.Open(settings);
    ```

## Reading files

=== "C++"

    ```cpp
    // By path (MPQ, or CASC through the listfile).
    auto map = fs->read_file("DBFilesClient/Map.dbc");

    // By FileDataID on a CASC client.
    auto blp = retail->read_file(wowlib::FileDataID{189077});

    // A FileKey can carry either half; resolve() fills in the other
    // from the listfile.
    auto key = retail->resolve(wowlib::FileKey{"Interface/FrameXML/UIParent.lua"});
    ```

=== "Python"

    ```python
    # By path (MPQ, or CASC through the listfile).
    map_bytes = fs.read_file("DBFilesClient\\Map.dbc")

    # By FileDataID on a CASC client.
    blp = retail.read_file(wowlib.FileDataID(189077))

    # A FileKey can carry either half; resolve() fills in the other
    # from the listfile.
    key = retail.resolve(wowlib.FileKey("Interface/FrameXML/UIParent.lua"))
    ```

=== "C#"

    ```csharp
    // By path (MPQ, or CASC through the listfile).
    byte[] map = fs.ReadFile(@"DBFilesClient\Map.dbc");

    // By FileDataID on a CASC client.
    byte[] blp = retail.ReadFile(new FileDataId(189077));

    // A FileKey can carry either half; Resolve() fills in the other
    // from the listfile.
    var key = retail.Resolve(new FileKey("Interface/FrameXML/UIParent.lua"));
    ```

`exists()` takes the same three shapes as `read_file()`. Paths are
case-insensitive and both slash directions work — they are canonicalized
before lookup.

## Beyond reading

- **Writing back**: pass a `project_directory` in the settings and writes land
  there as loose files, overlaying the client — the standard modding workflow.
  Reads prefer the project directory over the archives.
- **Encrypted retail content**: some CASC-era files are TACT-encrypted; import
  community key lists with `import_keys()` and the storage hands back decrypted
  bytes wherever a key matches.
- **Custom FileDataIDs**: on CASC-era projects, `custom_fdid_start` reserves a
  range for files your mod adds; the listfile records them.

The format entities all take the `FileSystem` + `FileKey` pair directly —
`table.read(fs, key)`, `wmo.read(fs, key)` — so after this page you rarely
touch raw bytes yourself. See **[Filesystem](../python/fs.md)** for the full
surface.
