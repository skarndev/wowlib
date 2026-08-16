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

## ClientDB is ONE welded class now (2026-08-16 rework)

The generic runtime-schema `db::DynTable` (welded as **`Table`**) + `Column`/
`ColumnType` replace the ~8.5k generated per-era table classes entirely
(plan: fluffy-twirling-hickey; .claude/context/db-architecture.md has the
engine story). `surface.hpp` includes `<wowlib/db/dyn_table.hpp>` + the
shared value types; there is no table umbrella, no cs_aliases, no dbdgen
C++ involvement in the C# surface at all. 61,384 → **4,005 P/Invokes**;
dylib 99 → **11 MB**; the managed wrapper compiles in ~13 s.

Typed access ships as dbdgen-emitted PURE C# facades (`--cs-facades-out`,
16 `Facades.<i>.cs`, ~6.6 MB): per (table x range) a `<Table><Suffix>Row`
struct with accessors at the range's FIXED column indexes + a static
opener with the era-canonical ClientVersion. No interop of their own;
packed into the NuGet via welder-csharp's nuget `EXTRA_COMPILE` globs
(297fb03). Doc text XML-escapes (CS1570 otherwise).

## Shim sharding

`welder_csharp_generate_bindings(... SHARDS N)` (default 16 now) splits the
shim into parallel TUs; each shard includes `surface.hpp` whole (~20 s per
TU post-rework). SHARDS > 1 contract: namespace-scope function definitions
in welded headers must be `inline` (ordinary ODR).

## Generator sharding BY FORMAT (the 11-minute serial TU fix)

The single-TU generator spent ~11 serial minutes reflecting the formats
version matrices. It is multi-TU by FORMAT now (welder-csharp
`begin_document / at / contribute / render_files`):

- `gen.cpp` (main): walks `^^wowlib` over `surface_core.hpp` — everything
  EXCEPT the per-range alias tables (per-TU visibility is the partition:
  the walk only welds what the TU declares). ~3 min.
- `gen_{wmo,m2,adt,wdt,wdl}.cpp`: each includes ONLY its format's
  `instantiations/<fmt>_ranges.hpp` and welds the version-matrix aliases
  EXPLICITLY (`weld_type<^^Alias>` into `wcs::rod::at(doc, "Formats.X...")`
  — no namespace walk, so the chunk classes the main TU welds never
  double). The X-macro tables in the ranges headers are the single source
  of truth; the family→namespace map matters (wdt families live in
  root/occlusion/lights/fogs/mpv sub-namespaces). The version-INDEPENDENT
  payload aliases (M2SplineKey*/FBlock*/M2PartTrackFixed16) are standalone
  `using`s outside the X-tables and ride gen_m2 explicitly — missing them
  leaks raw C++ spellings into the wrapper (CS7000).
- Wall = max(contributor): m2 ~291 s is the long pole; wmo 113 s, rest <45 s.

Gotchas that cost a debugging cycle each:
- A welded class's public NESTED types are bound with it — a nested struct
  with a `std::span` member (PodColumnView) fails the gate; move it to
  namespace scope, unannotated.
- An AGGREGATE welded class gets a synthesized field-wise ctor; a
  `const char*` member would store a pointer into the marshalling temp.
  `Column` declares a defaulted ctor precisely to not be an aggregate.

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
