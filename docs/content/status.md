# Format support

wowlib targets the **last minor release of every major expansion**, Vanilla
through The War Within. Each format is modelled for all versions in one
versioned entity; the matrix below shows how far each has been *verified*.

**Round-trip guarantees** — formats whose containers allow it are
**byte-perfect** (`write(read(bytes)) == bytes`); offset-table formats are
**semantic** (`read(write(x)) == x`, canonical relayout on write). Unknown and
undocumented chunks are preserved verbatim, never dropped.

- ✅ — implemented, round-trip verified against a real client's file corpus
- 🟡 — implemented, awaiting a client install to verify against
- ➖ — not applicable to that client

| Format | Round-trip | Vanilla<br>1.12.1 | TBC<br>2.4.3 | WotLK<br>3.3.5a | Cata<br>4.3.4 | MoP<br>5.4.8 | WoD<br>6.2.4 | Legion<br>7.3.5 | BfA<br>8.3.7 | SL<br>9.2.7 | DF<br>10.2.7 | TWW<br>11.2.7 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **WMO** (root + groups) | byte-perfect | 🟡 | 🟡 | ✅ | 🟡 | 🟡 | 🟡 | 🟡 | 🟡 | ✅ | 🟡 | 🟡 |
| **M2** (+ `.skin` `.anim` `.skel` `.bone`)[^m2] | semantic | 🟡 | 🟡 | ✅ | 🟡 | 🟡 | 🟡 | 🟡 | 🟡 | ✅ | 🟡 | 🟡 |
| **ADT** (root + split files)[^adt] | semantic | 🟡 | 🟡 | ✅ | 🟡 | 🟡 | 🟡 | 🟡 | 🟡 | ✅ | 🟡 | 🟡 |
| **WDT** (+ `_occ` `_lgt` `_fogs` `_mpv`) | byte-perfect | 🟡 | 🟡 | ✅ | 🟡 | 🟡 | 🟡 | 🟡 | 🟡 | ✅ | 🟡 | 🟡 |
| **WDL** | byte-perfect | 🟡 | 🟡 | ✅ | 🟡 | 🟡 | 🟡 | 🟡 | 🟡 | ✅ | 🟡 | 🟡 |
| **BLP**[^blp] | byte-perfect | 🟡 | 🟡 | ✅ | 🟡 | 🟡 | 🟡 | 🟡 | 🟡 | ✅ | 🟡 | 🟡 |
| **DBC / DB2**[^db] | byte-perfect (WDBC/WDB2), semantic (WDC*) | ✅ WDBC | ✅ WDBC | ✅ WDBC | ✅ WDB2 | 🟡 WDB2 | 🟡 WDB2 | 🟡 WDC1 | 🟡 WDC3 | ✅ WDC3 | 🟡 WDC5 | 🟡 WDC5 |
| **MPQ** storage & patch chain[^mpq] | — | ✅ | ✅ | ✅ | ✅ | 🟡 | ➖ | ➖ | ➖ | ➖ | ➖ | ➖ |
| **CASC** storage | — | ➖ | ➖ | ➖ | ➖ | ➖ | 🟡 | 🟡 | 🟡 | ✅ | 🟡 | 🟡 |

[^m2]: `.phys` is carried as an opaque blob for now (structured records
planned). Semantic round-trip means the writer relays out offset tables
canonically, so output is equivalent, not byte-identical.
[^adt]: Monolithic (WotLK-) and split (Cata+ `_tex0`/`_obj0`/`_obj1`/`_lod`)
layouts both supported; `_obj1`/`_lod` LOD chunks currently round-trip verbatim
as raw chunks, structured access planned.
[^blp]: BLP is identical across all client versions; verification spans the
corpora we have locally. JPEG-encoded BLPs round-trip verbatim but do not
decode.
[^db]: Typed record structs for **1221 tables** are generated from
[WoWDBDefs](https://github.com/wowdev/WoWDBDefs) for every era. WDC4
(10.1–10.2.5) is implemented as well. Encrypted (TACT) DB2 sections are
preserved and reported; key injection is wired but not yet exercisable locally.
[^mpq]: The 3.3.5a patch chain was verified against the client binary itself
(reverse-engineered); the MoP chain table awaits a 5.4.8 install.

## Classic clients

WoW Classic Era, the Classic progression realms (BCC, WotLK, Cataclysm, MoP
Classic) and the Anniversary realms are **not** old clients — they are the
modern client rebuilt from whatever retail branch was current, shipping
old-looking content. Classic Era 1.15.9 is a Midnight-era CASC client; Cata
Classic 4.4.2 writes War Within-era files. One version *number* can even span
two engines (4.4.0 shipped on Dragonflight and, months later, on The War
Within), and `wow_classic_titan` calls itself 3.80.

wowlib keys these off the **build number** — Blizzard's counter is global
across every product — via a `flavor` on `ClientVersion` and the
`format_lineage` it implies, so a Classic client resolves to whichever retail
column above its files actually match. `ClientInstall.detect` reads the exact
version and product code off an installation; see
[Classic clients](guide/version-agnostic.md#classic-clients).

Coverage therefore rides on the retail columns. There is no Classic install
locally, so the build → engine mapping is covered by unit tests rather than a
file corpus.

## Not yet implemented

- Structured M2 `.phys` records
- Structured ADT `_obj1`/`_lod` (and blend-mesh) chunks
- TACT decryption of encrypted DB2 sections
- Cross-version format conversion (scaffolding only)
- Lua bindings (annotated throughout, module deferred)
- Hotfix caches (`DBCache.bin`)
- Standalone formats not yet started: `.tex`, `.lit`, `.wlw`/`.wlq`/`.wlm`, …
