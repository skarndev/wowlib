# C#/.NET bindings (welder C# rod)

Read when: touching `bindings/csharp/`, the `weld` annotations, or the
`mark::exclude(lang::cs)` sites.

## The language-agnostic weld convention

Every welded entity carries a **bare** `[[=welder::weld]]` — never
`weld(welder::lang::py, welder::lang::lua)`. welder reads an empty language mask
as "all languages", so one annotation serves every rod the project builds and a
new backend costs no annotation churn. (This is why the C# target could be added
without touching 255 annotation sites; see the `bare-weld-convention` memory.)

Only **marks** may name a language, and only where the reason is genuinely
language-specific. There are exactly two kinds in the tree:

- `mark::only(welder::lang::lua, welder::lang::cs)` on the 12 per-version
  `read`/`write` fs-I/O methods (WMO, M2, Skeleton, ADT, WDT, WDL). Python
  attaches an equivalent `read/write/convert/for_version` surface to the
  version-agnostic base (`WMOBase`, …) from hand-written module glue, so its
  per-version classes stay pure data; Lua and C# have no such glue and take the
  direct methods. **These were `lua`-only before C# existed** — widening them is
  what gives a C# caller any way to load a file at all; if a C# facade layer is
  written later (the Python `formats/*.cpp` glue equivalent), revisit.
- `mark::exclude(welder::lang::cs)` on the members whose type the C# rod cannot
  marshal yet (below). Each site says why.

## What the C# rod cannot marshal yet

welder's C# rod covers scalars, strings, `std::filesystem::path`, `std::byte`,
welded classes/enums, `optional`, `pair`/`tuple`, flat scalar/enum sequences,
reference-semantic containers of welded classes, maps, smart pointers,
`std::expected` (unwrapped — the error branch throws) and inbound `std::span`.
It does **not** yet have a wire form for:

| Shape | wowlib members excluded for `cs` |
|---|---|
| `vector<vector<T>>` | `MapChunk::alpha_maps`; `M2Track::timestamps`/`values`; `M2TrackBase::timestamps`; `WDTParticulates::point_groups`/`bound_groups` |
| `vector<array<T, N>>` | `M2ChunkedFile::texture_ac`, `M2SkinProfile::bones` |
| `vector<string>` | `RoundtripReport::unknown_chunks`, `FileSystem::enumerate_paths()` |

One further exclusion is a **welder bug workaround**, not a missing type family:
`ChunkedFile::read(span)` and `ChunkedFile::write()`. They are flattened onto
every versioned entity, which ALSO welds a per-version `read(fs, key)` /
`write(fs, key)` for `cs` — so C# sees two `read` overloads whose declaring
scopes are both class-template specializations. Those have no spellable name, so
welder's C# rod anchors both on the bound type and mangles both to the same C
symbol (`..._m_read_0`); its duplicate-symbol `#error` catches it. The fs-level
pair is the one C# keeps — it is the only way to load the multi-file entities
(WMO groups, M2 satellites, split ADTs). Drop the excludes once welder indexes
overloads over the same flattened sequence its shim-side lookup searches.

Drop the `mark::exclude(welder::lang::cs)` at those sites when welder grows the
family. Everything else in the C++ API binds.

## Result<T> is the exception channel

`Result<T>` (`std::expected<T, Error>`) does **not** surface as a result object.
The C# rod peels the expected: the value branch crosses as plain `T`, the error
branch throws through the `welder_error` slot every thunk carries, so C# sees
`T Method()` and a `try/catch`. welder renders the error text through an ADL
`to_string(e)` first — so `wowlib::Error` should keep/gain one if the message
quality matters (without it welder falls back through `.what()`, string-ness,
`std::formatter`, `operator<<`).

## Build shape

```bash
cmake -S . -B build/csharp -G Ninja -DCMAKE_CXX_COMPILER=g++-16 \
  -DWOWLIB_BUILD_TESTS=OFF -DWOWLIB_BUILD_CSHARP=ON
cmake --build build/csharp --target wowlib_cs
```

- `WOWLIB_BUILD_CSHARP` (default OFF) gates `bindings/CMakeLists.txt`'s C# block.
- **No .NET SDK is needed to build.** The rod is pure reflection→text; `dotnet`
  is only needed to *consume* the emitted `Bindings.cs`.
- Two artifacts land in `build/csharp/bindings/csharp/`: `shim.cpp` (compiled
  into `libwowlib_native.dylib`) and `Bindings.cs`.
- `bindings/csharp/surface.hpp` is the single header BOTH the generator and the
  generated shim reflect, so the two TUs cannot see different declarations. It
  pulls in the per-range welded alias tables from
  `bindings/python/instantiations/*_ranges.hpp` — those are language-neutral and
  should be hoisted to `bindings/instantiations/` when it is worth touching the
  Python target's include paths.
- Both TUs are as reflection-heavy as `wowlib_py`'s module TU (multi-GB, minutes
  each), so they share the `wowlib_py_compile` Ninja job pool.

## Local toolchain notes

- macOS/arm64, gcc-16 + .NET 10 SDK. welder's cookbook/test csproj files default
  to `net8.0`; with only the .NET 10 runtime installed a `net8.0` app fails to
  launch — target `net10.0` (or install the 8.0 runtime).
