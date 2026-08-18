# Core & versions

The top-level `wowlib` module holds the shared vocabulary — client versions and
expansions, locales, file keys, and the exception hierarchy — used across the
filesystem and format layers.

::: wowlib
    options:
      show_root_heading: false
      show_root_toc_entry: false
      filters:
        - "!^_"
        - "!^Vector"

## Version constants (`wowlib.versions`)

One ready-made `ClientVersion` per finished expansion — the exact
last-minor-of-major build wowlib targets — so code never spells build numbers:

```python
from wowlib import versions
from wowlib.fs import FileSystem, FileSystemSettings

fs = FileSystem.open(FileSystemSettings("/Games/WoW 3.3.5a", versions.wotlk))
```

There is also one per living Classic product line (`classic_era`,
`classic_bcc`, `classic_wotlk`, `classic_cata`, `classic_mop`,
`anniversary`). Those are modern-engine clients wearing legacy version
numbers, so their `flavor` is not `Retail` and their file formats follow
`format_lineage` — the retail branch their build was cut on — rather than
their `major`. Since those products ship new builds continuously, the
constants are snapshots: for a real installation prefer
`ClientInstall.detect`.

::: wowlib.versions
    options:
      show_root_heading: false
      show_root_toc_entry: false
