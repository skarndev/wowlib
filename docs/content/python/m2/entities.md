# M2 entities

The file-level entities: the `M2` assembly (body + baked satellites), the
`M2Data` MD20 body, the external `Skin` LOD view, the Legion+ `M2File`
chunked shell, the shared `Skeleton` and the `.bone` facial-pose file.
Construct a concrete version with `for_version(expansion)`; `read`/`write`
speak the filesystem gateway.

::: wowlib.formats.m2
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 2
      filters:
        - "!^records$"
