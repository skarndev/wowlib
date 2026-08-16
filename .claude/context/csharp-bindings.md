# C#/.NET bindings (welder-csharp rod)

Read when: touching `bindings/csharp/`, the `weld` annotations, or any
`wowlib::lang::cs` site.

## The rod is an out-of-tree welder extension now (2026-08)

The C# rod was extracted from welder's `feature/csharp` branch into its own
repo, https://github.com/skarndev/welder-csharp; welder is pinned at **main**
again. `cmake/Dependencies.cmake` declares both: `welder_csharp` is
MakeAvailable'd only under `WOWLIB_BUILD_CSHARP`, and because our welder is
populated first, its `if(NOT TARGET welder::headers)` guard uses our pin
rather than fetching its own. The subproject defines
`welder_csharp_generate_bindings()` globally — no `CMAKE_MODULE_PATH` dance.

## Language identity: `wowlib::lang::cs`

welder's core no longer names C# (`welder::lang` holds only `py`/`lua`; the
old `welder::lang::cs` spelling does not compile). The rod mints its identity
from welder's open user range (`user_lang<WELDER_CSHARP_LANG_SLOT>`, default
slot 0) as `welder::rods::csharp::cs`. wowlib **respells the same identity**
in `src/wowlib/core/lang.hpp` as `wowlib::lang::cs` (`user_lang<0>`), so core
headers never include rod headers — they must parse in Python-only builds
where welder-csharp is not even fetched. The two spellings must stay
bit-for-bit equal: if the rod's slot is ever re-pointed, re-point ours.

## The language-agnostic weld convention

Every welded entity carries a **bare** `[[=welder::weld]]` — an empty language
mask means "all languages", any rod included. Only **marks** may name a
language, and only where the reason is genuinely language-specific. The `cs`
sites in the tree today:

- `mark::only(welder::lang::lua, wowlib::lang::cs)` on the 12 per-version
  `read`/`write` fs-I/O methods (WMO, M2, Skeleton, ADT, WDT, WDL). Python
  attaches an equivalent surface to the version-agnostic bases from
  hand-written glue; Lua and C# take the direct methods.
- `weld_as(wowlib::lang::cs, ...)` on two C#-only naming collisions the rod
  diagnoses at generation: `SMTextureColorGrading::_04` → `Unknown04` (leading
  underscore survives PascalCase and collides with the generated `_h_*`
  scaffolding namespace), and `SMOFog::Fog` → `FogBand` (CS0102: a nested type
  and the `fog` member both style to `Fog`).

There are **no `mark::exclude(cs)` sites left**: the previously-unmarshallable
shapes (`vector<vector<T>>`, `vector<array<T,N>>`, `vector<string>`) and the
flattened-base `read`/`write` overload collision were all fixed in the rod
before the extraction, and the excludes were dropped. The whole C++ API binds.

## ClientDB tables are on the C# surface

`bindings/csharp/surface.hpp` includes `<wowlib/db/tables/all.hpp>` — a
dbdgen-emitted umbrella over every generated table header (language-neutral:
nothing but includes; `emit_all_tables` in tools/dbdgen). The CMake block
links `wowlib_db_tables` (include root) and adds explicit `add_dependencies`
on `wowlib_dbdgen` for both the generator and shim targets, since an INTERFACE
link alone does not order compilation after header generation.

## Shim sharding (what unblocked the DB-bearing build)

One shim TU over formats + ~1200 tables x all eras does not compile in real
time/memory. `welder_csharp_generate_bindings(... SHARDS N)` splits the shim
into `shim.0.cpp … shim.N-1.cpp` that compile in parallel; each top-level
class's thunks land whole in one shard, so the split is always link-correct.

- `WOWLIB_CS_SHARDS` (cache var, default 32). Unlike the Python db shards,
  every C# shard includes `surface.hpp` **whole**, so each shard pays the full
  frontend parse — that redundancy is why the default sits well below
  `WOWLIB_DB_SHARDS`' 96.
- All shim TUs plus the generator TU run in the `wowlib_py_compile` job pool
  (the RAM-bounded Ninja pool, computed at the top of bindings/CMakeLists.txt
  outside both backend blocks).
- SHARDS > 1 contract: the welded headers are included by several TUs, so
  namespace-scope function definitions must be `inline` (ordinary header ODR
  rule; wowlib headers already satisfy it — they compile into many Python TUs).

## Generator sharding (the RAM fix, 2026-08-16)

Shim sharding split the *output*; the generator itself was still ONE TU
welding the whole surface — 2388 s / ~16 GB peak, which is what forced the
swap hack on GH runners. It is now multi-TU via welder-csharp's
`begin_document / at(doc, "Ns.Path") / contribute_namespace / render_files`
(the document is runtime state — symbol registry and type-name placeholders
are global across contributors, so cross-TU references resolve at render
time). Measured peak cc1plus: **2.07 GB** (was ~16), shards compile in
parallel.

- `bindings/csharp/gen.cpp` is a manual `main()`: `begin_document`,
  `contribute_namespace<^^wowlib>` over `surface_gen.hpp`, then
  `wowlib_cs::db::contribute_all_tables(doc)` (the shard registry), then
  `render_files`.
- dbdgen (`--cs-gen-out`, `--cs-gen-shards` = `WOWLIB_CS_GEN_SHARDS`,
  default 32) emits `cs_gen_shard_<i>.cpp` TUs + the registry header into
  `build/.../generated/db_cs_gen/`. NOT the Python shard count (96): every
  gen shard parses the full alias umbrella, so shard count multiplies parse
  waste; RAM per TU shrinks with more shards (2.07 GB peak measured at 96). Each shard welds its tables
  explicitly through the per-range aliases in
  `<wowlib/db/tables/cs_aliases.hpp>` using welder's `weld_type<^^Alias>`
  overload (alias reflection as the Decl anchor — REQUIRED: passing the
  dealiased type spawned identical `detail_` C symbols in every shard TU and
  the shim failed to link on duplicates).
- `surface_gen.hpp` (generator main TU) vs `surface.hpp` (shim TUs): the
  generator's namespace walk must NOT see the table headers or every table
  would weld twice (walk + shard). But it MUST weld the db SHARED types the
  tables reference — TableBase, LocString8/16, the codec types. Those came
  transitively through `all.hpp` in the single-TU world; `surface_gen.hpp`
  includes `<wowlib/db/locstring.hpp>` + `<wowlib/db/table_core.hpp>`
  explicitly. Symptom when missing: the wrapper compiles the raw C++
  spelling into C# (`: ::wowlib::db::TableBase`, `LocString<8>Handle`) →
  CS7000 "unexpected use of an aliased name".
- The shim (32 TUs) still includes `surface.hpp` whole — unchanged.
- CMake: `WelderCSharpModule.cmake`'s `EXTRA_GEN_SOURCES` links the shard
  TUs into the generator executable; wired in `cmake/DbTables.cmake`
  (`WOWLIB_CS_GEN_SHARD_SOURCES`) + `bindings/CMakeLists.txt`.

## Erased fields (welder-csharp b97a9d7, 2026-08-16)

The C# twin of the Python db erasure rounds. Eligible data members bind
through ~25 FIXED entry points (`welder__field_get_int(handle, off)`, one
typed load/store per scalar width + bool + `std::string` pair + the
live-view `welder__field_addr`) instead of one extern-C thunk + one
`[LibraryImport]` per accessor per member. The managed property carries the
generator-computed byte offset; the shim carries one
`static_assert(wcs::shim::nsdm_offset<T>(idx) == off)` per erased member,
so a platform whose ABI lays the class out differently (e.g. a `long`
member crossing LP64/LLP64) fails the shim build instead of misreading.

- Eligible: scalars, bool, enums (underlying-width stub + managed cast),
  exactly-`std::string`, and the GETTER of non-const handle-like members
  (class/seq_ref/map_ref — `self + off` live view) + the scalar-seq live
  wrapper's getter. Setters of handle-like/container members stay bespoke
  (`field_set`/`field_assign` need the member's own type). Flattened base
  members, bitfields, reference members, `string_view`/`path`, const class
  members: bespoke.
- `SafeHandle` (the abstract base) is a legal `[LibraryImport]` param —
  verified — so the shared stubs take any per-class handle with full
  premature-collection safety.
- `options.erased_fields` (default ON) restores the old artifact when off.
- Why it matters here: the full DB surface was 111,742 P/Invokes bespoke
  (72 MB wrapper; csc + interop source generator ran >80 min locally and
  never finished before we killed it). Erasure collapses it to ~20k.

## Result<T> is the exception channel

`Result<T>` (`std::expected<T, Error>`) does **not** surface as a result
object. The rod peels the expected: the value branch crosses as plain `T`, the
error branch throws through the `welder_error` slot every thunk carries.
welder renders the error text through ADL `to_string(e)` first — `wowlib::Error`
has one.

## How a consumer takes it: the NuGet package

A .NET user never compiles `Bindings.cs`. `welder_csharp_nuget_project` writes
a packable csproj (`build/csharp/bindings/Wowlib/Wowlib.csproj`); after the
native build:

```bash
dotnet pack build/csharp/bindings/Wowlib/Wowlib.csproj -c Release -o out
```

produces (measured 2026-08-14): a **6.0 MB** package holding
`lib/net8.0/Wowlib.dll` (7.9 MB — the compiled wrapper),
`lib/net8.0/Wowlib.xml` (1.9 MB, 9807 `<summary>` entries) and
`runtimes/osx-arm64/native/libwowlib_native.dylib` (16 MB). The consumer adds
the package reference; the .NET host resolves the native library by RID. The
RID is the packing platform's — a cross-platform package means packing on each
platform and merging the `runtimes/` trees, or per-RID packages from CI.

**IntelliSense needs no sources.** Signatures come from the assembly's own
ECMA-335 metadata; the doc TEXT is stripped from IL into the `.xml` sidecar,
which the IDE joins by key (`T:`/`M:`/`F:`). So the chain is
`[[=welder::doc]]` → `/// <summary>` in the generated C# → `Wowlib.xml` → the
tooltip. It only works while the `.xml` travels beside the `.dll` (inside a
package it always does; a hand-copied bare DLL loses the descriptions).
`GenerateDocumentationFile` in the csproj is what produces it — without it the
package silently shipped none. The dylib contributes nothing here: it has no
metadata, and is loaded only at the first P/Invoke.

## Wrapper splitting (WOWLIB_CS_FILES)

`CS_FILES 24` emits `Bindings.0.cs … Bindings.23.cs` (largest ~958 KB instead
of one 11 MB file), so the generated surface can be opened, diffed and
reviewed. It is **not** a build-speed measure — a C# assembly has no
translation-unit boundary and Roslyn compiles one big file and many small ones
in the same time (measured: 39s vs 40s). The rod cuts only at boundaries its
emitters record, so a part never ends mid-declaration; `NativeMethods` is
`partial` and each part reopens whatever scope it needs.

## Build shape

```bash
cmake -S . -B build/csharp -G Ninja -DCMAKE_CXX_COMPILER=g++-16 \
  -DWOWLIB_BUILD_TESTS=OFF -DWOWLIB_BUILD_CSHARP=ON
cmake --build build/csharp --target wowlib_cs
```

- `WOWLIB_BUILD_CSHARP` (default OFF) gates both the welder-csharp fetch and
  `bindings/CMakeLists.txt`'s C# block.
- **No .NET SDK is needed to build.** The rod is pure reflection→text;
  `dotnet` is only needed to *consume* the emitted `Bindings.cs`.
- Artifacts in `build/csharp/bindings/csharp/`: `shim.<i>.cpp` (compiled into
  `libwowlib_native.dylib`) and `Bindings.<i>.cs`.
- `bindings/csharp/surface.hpp` is the single header BOTH the generator and
  the generated shim reflect, so the TUs cannot see different declarations. It
  pulls the per-range welded alias tables from `bindings/instantiations/`
  (bindings root — language-neutral, shared by all backends) plus the db
  umbrella.

## Local toolchain notes

- macOS/arm64, gcc-16 + .NET 10 SDK. welder-csharp's cookbook/test csproj
  files default to `net8.0`; with only the .NET 10 runtime installed a
  `net8.0` app fails to launch — target `net10.0` (or install the 8.0
  runtime).
