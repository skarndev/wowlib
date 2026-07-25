# WMO format

The **WMO** (World Model Object) format — buildings, caves, dungeons and other
static world geometry — modelled as versioned entities. `WMO` is the compound
user-facing class (the root file plus its group files, baked into one object);
each expansion has a concrete subclass (`WMOWotlk`, `WMOShadowlands`, …)
exposing that version's chunk layout.

Pages:

- **[WMO entity](entity.md)** — the user-facing compound `WMO` (the assembly
  with the root and every group file baked in), read/written through the
  filesystem gateway.
- **[WMO root](root.md)** — the root file: every field with the expansion range
  it is available in (generated from the sources, so the ranges never drift)
  and its chunk FourCC.
- **[WMO group](group.md)** — the group files (`WMOGroup` wrapper +
  `WMOGroupBody`), each field with its FourCC and expansion badges.
- **[Root chunks](root-chunks.md)** — the wire structs used by the root file.
- **[Group chunks](group-chunks.md)** — the wire structs used by a group file.

## Version-agnostic unions

Each family exposes an `Any…` **type alias** — the union of all its per-version
classes, bound as a real `types.UnionType` on its module (importable, and usable
in `isinstance` on Python ≥ 3.10). It is the runtime return type of
`for_version(expansion: Expansion)` and the natural annotation when your code
handles any version:

```python
from wowlib.formats.wmo.root import AnyWMORoot   # WMORootVanillaToWod | … | WMORootTheWarWithin

def process(root: AnyWMORoot) -> None:
    ...

root = WMORoot.for_version(pick_expansion())     # statically typed as AnyWMORoot
```

| Alias | Module | Union of |
|---|---|---|
| `AnyWMO` | `wowlib.formats.wmo` | every `WMO⟨version⟩` |
| `AnyWMORoot` | `wowlib.formats.wmo.root` | every `WMORoot⟨version⟩` |
| `AnyWMOGroup` | `wowlib.formats.wmo.group` | every `WMOGroup⟨version⟩` |
| `AnyWMOGroupBody` | `wowlib.formats.wmo.group` | every `WMOGroupBody⟨version⟩` |

The versioned chunk structs follow the same pattern — `AnyWMOGroupHeader` and
`AnyWMOBatch` in `wowlib.formats.wmo.group.chunks`.

See the guide's **[Reading a WMO](../../guide/wmo.md)** for a worked example.
