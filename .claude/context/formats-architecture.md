# Formats subsystem architecture

Chunked-format serialization (`src/wowlib/formats/`). Milestone 1 (2026-07-19,
commits b986d1d + 22199bc): chunk framework + WMO for wotlk + shadowlands, with
Python/Lua bindings. Plan of record: `~/.claude/plans/mighty-swimming-falcon.md`.

## Core design

- **Version axis**: `ClientVersion` as NTTP (`Wmo<versions::wotlk>`). Layout
  changes = constrained partial specializations (`requires (V < boundary)`);
  chunk-set changes = `since()`/`until()` annotations on entity members.
  Format-specific boundary builds live next to the format (`wmo_fdid_refs`,
  `wmo_split_groups`, `wmo_batch_large_material` in wmo_wire.hpp).
- **Byte-perfect round-trip is a tested guarantee**: reads are chunk-order
  independent (fourcc dispatch); every encounter lands in `chunk_extras::journal`;
  writes replay the journal; unknown/duplicate/capacity-overflow chunks are
  preserved verbatim in `unknown[]`; <8 trailing stray bytes kept. Fresh
  entities (empty journal) write declaration order — entities declare members
  in canonical client chunk order.
- **Member kinds are type-driven** (serializer.hpp dispatch): trivially-copyable
  struct = data chunk (exact size), `std::vector<T>` = array chunk
  (divisibility), `string_block` (raw blob is source of truth — offsets stored
  in other chunks stay valid), `chunk_blob` (opaque), `repeated<T,N>`
  (+`repeats(n)` annotation; MOTV×3, MOCV×2), nested ChunkedEntity
  (+`container`; MOGP), `header` members = raw prelude before a container's
  chunk stream.
- **Presence policy**: `optional` is per-member; wowlib marks nearly everything
  optional (only MVER/MOHD/MOGP required) because real files omit more than
  wowdev admits. Required chunks always write (clients ship size-0 chunks);
  optional ones write when non-empty. Optional plain-data chunks would be
  unobservable — none exist in supported formats (documented in `engaged()`).
- **Instantiation control**: per-format version list (`wmo::wmo_versions`),
  explicit instantiation in the format's .cpp, `extern template` in the .hpp —
  serializer expansion confined to one TU (~2 s per version for WMO).
- **Conversion** (convert.hpp): `convert<To>(src)` composes hand-written
  adjacent `convert_step` overloads along `supported_versions<E>`; identity
  copies; a missing step is a static_assert naming the pair. No real steps yet.

## Reflection findings (gcc 16.1, `-freflection`)

Canary: `tests/unit/test_reflection_spike.cpp` — keep compiling; minimal repro
of everything the engine stands on. Verified: annotations with ClientVersion
payloads on members of ClientVersion-NTTP templates read through instantiations
(`annotations_of_with_type` + `extract`, welder's idiom); `template for` +
splicing over such instantiations; constrained partial specializations stay
trivially copyable; explicit instantiation with `versions::` constants.
Gotchas: a `define_static_array` local used as a `template for` range inside a
function template must be `static constexpr`; annotations and standard
attributes cannot share one `[[...]]` list (`[[nodiscard]] [[=welder::doc]]`).
The fallback (consteval `chunk_map()`) was NOT needed.

## Bindings (no welder changes)

- Per-version classes weld through **namespace-scope aliases** (welder's
  designed route for template instantiations): `using WmoWotlk =
  wmo::Wmo<versions::wotlk>;` etc. at the end of wmo.hpp, flat in
  `wowlib::formats` (user choice). Version-invariant wire structs weld under
  their client names in the `formats.wmo` submodule (pep8: `SmoMaterial`).
  Keep aliases in dependency order and in sync with `wmo_versions`.
- **Factories** (`bindings/python/wowlib_module.cpp`
  `register_format_factories`): nanobind can't dispatch overloads on enum
  VALUES, so each per-version overload rejects foreign expansions with
  `nb::next_overload()`; each carries an explicit `nb::sig` with
  `Literal[wowlib.Expansion.X]` and the exact return class. stubgen renders
  them as `@overload` blocks; **mypy narrowing verified** (`Expansion` welds as
  `enum.IntEnum`, so `Literal` works). Catch-all overload raises
  `UnsupportedClientVersion`. `stub_patterns.nb` injects
  `from typing import Literal` (stubgen passes nb::sig verbatim, no auto-import).
- Wire structs use `std::array` instead of C-arrays (castable via
  `nanobind/stl/array.h`, layout guarded by size asserts). `chunk_extras`
  internals and `repeated<>` members are `mark::exclude`d (repeated needs a
  sequence protocol — M2 milestone). `Wmo::parse` excluded (span-of-spans).
- check.py: formats section incl. in-process mypy narrowing assertion
  (mypy lives in the project `.venv`); env-gated end-to-end byte-perfect
  rewrite through Python. Lua parity is automatic (`wowlib.formats.wmo_wotlk`).

## Known issues / deferred

- **Pre-existing (NOT formats)**: `bindings.python`/`bindings.lua` ctest fail
  on `FileSystemSettings` keyword defaults — the uncommitted welder pin bump
  (57e7747 → 19253c7) lost/changed the NSDMI-defaults synthesis (verified by
  stashing all formats work; failure reproduces). Part of the in-flight welder
  migration; fix belongs in welder or check.py expectations.
- Deferred to later milestones: M2/ADT/WDT; offset-based intra-chunk data
  (M2Array/MLIQ/MH2O — planned as an `offset_view` member kind in the same
  dispatch table); SL-only root chunks (MOUV/MDDI/MAVG/... — land in
  `unknown[]`, still byte-perfect; 9.2.7 histogram showed only MDAL + MOLP in
  group files); real convert steps; repeated<> bindings; zero-copy/ndarray
  vector exposure for Blender.

## Adding a new format (the recipe)

1. `formats/<fmt>/<fmt>_wire.hpp`: wire structs (trivially copyable,
   std::array not C-arrays, size static_asserts per supported version,
   welder::weld + doc). Boundary ClientVersion constants next to them.
2. `formats/<fmt>/<fmt>.hpp`: entities deriving `chunk_extras` with
   `static constexpr ClientVersion version = V;`, members annotated
   `chunk/since/until/optional/header/container/repeats` in canonical client
   order; multi-file assembly struct with load/save via fs::FileSystem;
   `<fmt>_versions` list + `supported_versions` specialization + aliases.
3. `formats/<fmt>/<fmt>.cpp`: out-of-line defs + explicit instantiations;
   add to root CMakeLists; extern template in the .hpp.
4. Tests: layout static_asserts; synthetic parse; integration round-trip
   (memcmp == 0) against real clients with the divergence diagnostic +
   unknown-chunk histogram pattern from test_wmo_roundtrip.cpp.
5. Bindings: factory overload per version in `register_format_factories`
   (+ the static_assert reminder there), stub OUTPUT entry if a new submodule.
