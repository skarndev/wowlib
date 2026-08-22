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
    using WoWLib;
    using Fs = WoWLib.Fs;
    using Versions = WoWLib.Versions;

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

## Detecting what is installed

Spelling the version out is fine when you know it. When you do not — and for
**any Classic client**, where the build number decides which engine's file
formats you get — read it off the installation instead. A CASC install
records its product code in `.flavor.info` beside `Data/`, and its exact
version in `.build.info` there or one directory up.

=== "C++"

    ```cpp
    #include <wowlib/fs/client_install.hpp>

    auto install = wowlib::fs::ClientInstall::detect(
      "/games/World of Warcraft/_classic_era_");
    if (!install)
      return report(install.error());

    // install->version   1.15.9.69109 (ClassicEra)
    // install->casc_product   "wow_classic_era"
    auto fs = wowlib::fs::FileSystem::open({.client_path = install->path,
                                            .version = install->version,
                                            .casc_product = install->casc_product});
    ```

=== "Python"

    ```python
    settings = wowlib.fs.FileSystemSettings.detect(
        "/games/World of Warcraft/_classic_era_",
        listfile_csv="/data/listfile.csv")

    with wowlib.fs.FileSystem.open(settings) as fs:
        print(fs.version)                # 1.15.9.69109 (ClassicEra)
        print(fs.version.format_lineage) # the retail engine its files follow

    # Or just the facts, without building settings:
    install = wowlib.fs.ClientInstall.detect("/games/World of Warcraft/_retail_")
    install.version, install.casc_product
    ```

=== "C#"

    ```csharp
    using var settings = Fs.FileSystemSettings.Detect(
        "/games/World of Warcraft/_classic_era_");
    using var fs = Fs.FileSystem.Open(settings);
    ```

Detection is CASC-only: MPQ-era clients (< 6.0) record no such file, and
neither do bare repacks that ship only `Data/`. Both raise `NotSupported` —
construct their `ClientVersion` directly, which is unambiguous anyway, since
no Classic product shares those version numbers *and* build range.

If `casc_product` is left unset, `FileSystem.open` derives it from the
version's flavor (`wow`, `wow_classic`, `wow_classic_era`,
`wow_anniversary`). Set it explicitly for PTR, beta and one-off products —
or let detection supply the exact code the installation recorded.

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

The filesystem remembers what it opened: `version` (the full client version —
the anchor for [version-agnostic code](version-agnostic.md)) and `kind`
(MPQ or CASC) are properties in every language.

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
