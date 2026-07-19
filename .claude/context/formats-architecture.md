# Formats subsystem architecture

Chunked-format serialization framework (`src/wowlib/formats/`). Milestone 1:
chunk framework + WMO (wotlk + shadowlands). Plan of record:
`~/.claude/plans/mighty-swimming-falcon.md` (approved 2026-07-19).

## Reflection spike findings (gcc 16.1, `-freflection`)

Canary: `tests/unit/test_reflection_spike.cpp` — keep it compiling; it is the
minimal reproduction of every corner the serializer engine stands on. Verified:

- **Annotations on members of a `ClientVersion`-NTTP class template** are
  readable through an instantiation with
  `std::meta::annotations_of_with_type(m, ^^spec)` +
  `std::meta::extract<spec>(anns[0])` (same idiom as welder's `reflect.hpp`).
  Annotation arguments may be namespace-scope `ClientVersion` constants
  (non-dependent); we never annotate with dependent expressions.
- **`template for` over `nonstatic_data_members_of(^^E, access_context::current())`**
  works for such instantiations, including member splicing `e.[:m:]` and
  `if constexpr` on per-member consteval annotation lookups.
- **Gotcha**: the `define_static_array` result used as a `template for` range
  inside a function template must be a `static constexpr` local — a plain
  `constexpr` local is rejected ("address may differ per invocation").
- **Constrained partial specializations on a `ClientVersion` NTTP**
  (`template <ClientVersion V> requires (V < boundary) struct S<V>`) work and
  stay trivially copyable; exact-size `static_assert`s hold.
- **Explicit instantiation** spelled with `versions::` constants
  (`template struct Entity<versions::wotlk>;`) works.
- The planned fallback (per-entity consteval `chunk_map()`) was NOT needed.

## Bindings direction (decided; no welder changes)

welder already welds class-template instantiations through **namespace-scope
aliases** (`using WmoWotlk = Wmo<versions::wotlk>;` — the alias supplies the
target name; `weld`/`weld_as` may sit on the alias). See welder CLAUDE.md
"class-template instantiation". So per-version Python classes need aliases, not
a custom registrar. Version-inactive members (since/until) welded anyway is the
accepted cosmetic fallback for M1; the versioned factory (`nb::sig` +
`Literal[Expansion.*]` overload stubs) is plain nanobind in wowlib_module.cpp.
