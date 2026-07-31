# BLP subsystem (formats/blp/)

Read when: touching BLP code, texture decode/encode, or stb_dxt.

## Shape (2026-07-30, initial ship)

- BLP2 is **unversioned** (identical vanilla → TWW) and **not chunked**: one
  welded `BLP` class in `formats/blp/blp.hpp`, NO ClientVersion axis, no
  facade/for_version, no instantiations TU, no chunks submodule. The magic is a
  FORWARD fourcc (`four_cc("BLP2", FourCCEndian::forward)`) — the reversed
  default silently mismatches (caught by a unit static_assert).
- BLP is the one formats/ module with a `.cpp` (allowed: non-templated class).
  `blp.cpp` is the ONLY TU that sees stb_dxt (STB_DXT_IMPLEMENTATION there).
- Entity = decomposed header fields + `palette` (std::array<CImVector, 256>,
  BGRX verbatim incl. garbage padding bytes) + `mips`
  (vector<FileBuffer>, `mark::exclude`d — vector<std::byte> members must NOT
  weld or the opaque generator/FileBuffer caster collide; Python raw access is
  `mip(level)`/`set_mip(level, bytes)`/`mip_count` instead) + excluded
  `stored_layout`.
- **Byte-perfect round-trip via layout replay**: read records the header's
  offset/size tables verbatim, the file size, and every uncovered byte run
  (gaps + trailing tail). write() replays that placement while each level's
  payload size is unchanged; any size change (or a fresh entity) falls back to
  the canonical contiguous layout. Same-size set_mip keeps the placement.
  Corpus-proven: 15 curated 3.3.5a + 60 sampled 9.2.7 files byte-perfect,
  every mip level decodes.
- Codecs (`codec.hpp`, detail::, AlphaMapCodec-style classes; definitions in
  blp.cpp): `DxtCodec` (state = resolved block format; own BC1/2/3/5 decoders,
  stb_dxt compression + in-house BC1 punch-through and BC2 explicit-alpha
  encoders — stb lacks both), `PaletteCodec` (state = alpha depth 0/1/4/8;
  median-cut `build_palette` built ONCE from level 0 and reused for all mips,
  memoized nearest-index), `RawCodec` (BGRA swizzle), `MipmapScaler` (box
  filter). Pixel layout everywhere: RGBA8 row-major top-left.
- DXT variant resolution (`resolve_dxt_format`): preferred_format when it
  names a block format (Dxt1/Dxt3/Dxt5/Bc5), else alpha-depth heuristic
  (<=1 -> BC1, 4 -> BC2, else BC3). Understated mip sizes (known client
  quirk, wowdev-documented) decode into transparent-black padding, never
  error.

## Corpus findings (survey-grounded)

- Shipped combos observed: Dxt/pf0/a0, Dxt/pf0/a1, Dxt/pf1/a8 (DXT3 ships
  alpha_depth **8**, not 4), Dxt/pf7/a8, Pal/pf8/a0, Pal/pf8/a1, Pal/pf8/a8.
- A 3.3.5a file shipped `Bgra` with **alpha_depth 136** — alpha_depth is NOT
  validated on read (any u8 stored + round-tripped); color_encoding (<= 4) and
  preferred_format (enumerated values only) ARE validated, since they feed
  welded enums (nanobind rejects out-of-range enum values).
- JPEG encoding (colorEncoding 0): never decodes/encodes (NotSupported), but
  round-trips verbatim.

## Deps

- stb_dxt: FetchContent URL pin of the single raw header at its last-touching
  commit (7023e27, 2021) + SHA256, DOWNLOAD_NO_EXTRACT; INTERFACE target
  `stb_dxt`, linked PRIVATE into wowlib. Provides compress-only BC1(opaque)/
  BC3/BC4/BC5.

## Bindings/docs

- Everything welds through the module walk automatically (wowlib.hpp
  pre-declares the `blp` namespace AFTER adt). Stub OUTPUT entry:
  `wowlib/formats/blp.pyi` (leaf submodule, single file). Image.pixels
  (vector<uint8_t>) rides the existing VectorUnsignedChar zero-copy wrapper;
  palette gets ArrayCImVectorx256.
- Docs: `content/python/blp.md` (plain mkdocstrings page — no version-badge
  engine, nothing is versioned) + nav row. pytest: tests/python/test_blp.py.
