# C++ conventions (project-wide)

House rules for wowlib C++. These are hard preferences, established 2026-07-27
during the ADT refactor; apply them to all new and touched code.

## No free-standing functions for important operations

A meaningful operation with internal strategy variants gets a **class** with a
small public verb surface (`decode`/`encode`/`read`/`write`) and its variants as
**protected** members — not a pile of free functions taking out-params. State
that is stable across a run (a format/mode) is a constructor member; per-call
inputs are parameters.
- Precedent: `adt::detail::AlphaMapCodec` / `ShadowMapCodec` (`formats/adt/codec.hpp`)
  replaced the old free `decode_alpha_4bit(span, vector&)` etc. Public
  `decode(src, compressed, fix)` / `encode(map, compressed, out)`; the bit-depth
  is the codec's state; `TerrainMapCodec` base holds the shared `fix_last_row_col`.
- Only **generic utilities** stay free functions: `has_flag`, `four_cc`,
  layout-math helpers in `formats/common/`. The test is "is this a reusable
  primitive, or a domain operation?" — domain operations become methods.

## Prefer std::ranges + monadic std::expected

- Use `std::ranges` algorithms over iterator-pair calls where they read cleanly:
  `std::ranges::sort(v)`, `std::ranges::none_of(v, pred)`, and
  `std::views::zip` / `std::views::enumerate` to walk parallel sequences
  (e.g. MapChunk layers ↔ alpha_maps in `read_split`). Don't force a view when a
  plain indexed loop is clearer (e.g. when an index may exceed a parallel
  vector's size).
- `Result<T>` is `std::expected`; use its **monadic** interface where it removes
  `if (!r) return …` noise: `fs.read_file(key).and_then([&](auto data){ … })`,
  `write_x().and_then([&](auto buf){ return add(path, buf); })`,
  `fs.add_file(…).transform([](auto&&){})` to drop a value. Keep the explicit
  `if (auto r = …; !r) return r;` form when several steps interleave and a lambda
  chain would hurt readability.

## No C-style casts, ever

Never `(T)x` or `T(x)` conversion casts. Use `static_cast` / `reinterpret_cast`
/ `std::bit_cast`. Discards use `std::ignore = expr` (for `[[nodiscard]]`
`Result`s written into a buffer), never `(void)expr`. `std::to_underlying(e)` for
enum→int, `has_flag(value, Enum::bit)` for flag tests.

## Full Doxygen everywhere

Every function/method carries a complete Doxygen block: one-line summary, then
`@tparam` for each template parameter, `@param` for each parameter, `@return`
for non-void returns. Wire structs and members use `welder::doc` per the
annotation policy (see [[doc-annotation-policy]] / chunk-annotation-conventions);
non-welded internals use `///`/`/** */` Doxygen.
