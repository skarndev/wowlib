# Filesystem

`wowlib.fs` is the storage gateway: one interface over **MPQ** archives (pre-WoD
clients) and **CASC** storages (WoD+ clients), plus the settings that configure a
client mount. Open a storage, resolve paths or FileDataIDs, read bytes.

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
