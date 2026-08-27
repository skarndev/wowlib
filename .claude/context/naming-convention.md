# Naming convention (2026-08-27 sweep)

The whole tree was renamed to the CLion scheme's rules (user decision 2026-08-26/27).
The inspection (`CppInconsistentNaming`, see clion-formatter-notes.md for the
headless recipe) is the oracle; ~2 900 declarations were renamed.

## The rules as applied
| Element | Convention | Example |
|---|---|---|
| Methods (all access), free functions, lambdas | `aaBb` | `readEntity`, `fourCc` |
| Parameters, locals | `aaBb` | `chunkSize` |
| Public fields, union members | `aaBb` | `areaId`, `flyingBounds` |
| Private/protected fields **and methods** | `_aaBb` | `_alphaDepth`, `_appendChunk` |
| Enumerators | `AaBb` | `HasMccv`, `Error` |
| Constants (global + static-const) | `AaBb` Pascal, **no k prefix** | `McvtCount`, `Vanilla` |
| Non-const globals | `gAaBb` | |
| Types | `AaBb_AaBb` (unchanged) | namespaces `aa_bb` (unchanged) |

The `k` prefix was REMOVED from both constants rules in the scheme (user
decision): `kVanilla` would weld into Python as `k_vanilla` and would mangle
the documented `builds::` convention. `versions::` welded constants are now
single-word Pascal (`Vanilla`, `Wotlk` — NOT `WotLK`, whose camel-split
`wot_lk` would break the Python name).

## Deliberate exemptions (residual inspection warnings, do NOT "fix")
- **Canonical client spellings**: types with acronym runs (`ADT`, `SMOHeader`,
  `FileDataID`, `C2iVector`, …), `fixed16`, locale enumerators (`enUS`…),
  hex enumerators (`Unk0x80`), `builds::` era names (`TBC`, `WotLK`,
  `BfA_Beta_25902` — numeric tails fail AaBb_AaBb). They bind verbatim into
  Python; renaming would corrupt domain identity. ReSharper C++ has no
  abbreviation list, so these warn forever.
- **External protocol names** (renaming breaks the framework SILENTLY via
  SFINAE or hook-by-name detection — the worst failure mode of the sweep):
  - nanobind type_caster: `from_python` / `from_cpp` (bindings/python/*caster*).
  - welder name-style hooks: `transform_class/enum/enumerator/...` and the
    opaque generator's `transform_opaque_container` (detected by
    `identifier_of(m) == "transform_opaque_container"` string compare).
  - STL/metafunction protocol members kept Pascal instead: trait `value` →
    `Value`, `ConcreteOf::type` → `Type` (ours, renamed consistently).
- **Welded protected lifetime surface**: `FileSystem::close()` / `isOpen()`
  stay un-prefixed — their identifiers ARE the Python (`close`, `is_open`)
  and C# (`Close`, `IsOpen`) names; `_close` would surface as `_close`.
- `RecordOpsFor<Record>`: the variable template was `record_ops` next to
  struct `RecordOps`; Pascalizing both collided, the instance is now *For*.

## Data-coupled names that must stay snake_case
- dbdgen-emitted record members mirror DBD column names (`map_name`) — the
  runtime schema lookup matches on them. tools/dbdgen/dbdgen/emit.py keeps
  `snake()` for members; its `_RESERVED` still holds "version"/"table_name"
  so escaped member spellings (`version_`) stay stable. The emitted statics
  are `Version`/`TableName`/`Schema`, eras via `cpp_era()` (`versions::Wotlk`).
- welder::doc strings and nb::name/nb::arg literals name the PYTHON surface —
  snake, do not rename.
- Reflection string compares name C++ members and were updated to camel:
  `identifier_of(m) == "flyingBounds"`, annotation member references
  (`indexesInRoot("doodadPlacements")`, `countMatches("collisionTriangles", 3)`,
  `offsetAfter(...)`, `memberOffset(...)`). Grep quoted snake strings after
  any future member rename.

## Consequences on the surfaces
- Python callables/fields keep snake_case via welder's style; kwargs too since
  welder a97833d (kwarg names previously bound raw `identifier_of` — the pin
  bump in cmake/Dependencies.cmake is REQUIRED for the sweep).
- Python ENUMERATOR members changed (bound verbatim by design):
  `ValidationSeverity.error` → `.Error`, `GroupFlags.exterior` → `.Exterior`, …
  Locale members (`enUS`) unchanged. Also `unk_4` → `unk4`-class fields and
  `versions.wotlk` stays (`Wotlk` → snake `wotlk`).
- Validation report labels/messages now use the camel C++ member names
  (framework derives them from identifier_of; handwritten labels updated to
  match). Python tests/audit expectations were updated.

## X-macro ranges
bindings/instantiations/*_ranges.hpp rows are `x(Suffix, Era)` — the second
arg is the `versions::` constant (Pascal); the Suffix builds the welded alias
(`ADTVanilla`) and is Python-facing.
