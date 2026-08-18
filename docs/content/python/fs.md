# Filesystem

`wowlib.fs` is the storage gateway: one interface over **MPQ** archives (pre-WoD
retail clients) and **CASC** storages (WoD+ retail and every Classic client),
plus the settings that configure a client mount. Open a storage, resolve paths
or FileDataIDs, read bytes.

`ClientInstall.detect` / `FileSystemSettings.detect` read a CASC installation's
own version and product code off disk — the reliable way to open a Classic
client, whose build number, not its version number, decides which engine's
files it carries.

`FileSystem` is a context manager: prefer `with FileSystem.open(settings) as fs:`
— the block exit calls `close()` for you, exception or not. Outside a `with`
block, pair `open()` with an explicit `close()` (see the examples on the class).

::: wowlib.fs
    options:
      show_root_heading: false
      show_root_toc_entry: false
      filters:
        - "!^_"
        - "^__(enter|exit)__$"
