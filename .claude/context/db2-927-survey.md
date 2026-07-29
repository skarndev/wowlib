# 9.2.7 DB2 corpus survey (2026-07-29)

Scratch tool `db2_survey.cpp` (kept in the session scratchpad) over the local
WoWCircle 9.2.7 CASC client + community-listfile.csv. Reads each
`dbfilesclient/*.db2`'s top header + section headers + field_storage_info.

**1298 db2 paths in the listfile; 835 locally readable** (the rest are CDN-only
on this repack). All findings below are over the 835 readable files.

## Magic — 100% WDC3

Every readable file is **WDC3**. Zero WDC4/WDC5. wowdev's WDC3 range is
8.1.0.28048 .. 10.1.0.48480, and 9.2.7 (build 45745) sits inside it — so the
earlier plan note "9.2.7 ships WDC4" was WRONG (the existing casc test's
`WDC3||WDC4` check is merely defensive). **Stage 3 only needs WDC3 for the
corpus.** WDC1 (7.3.5), WDC4/WDC5 (DF+) have no local client → synthetic-only,
later stages.

## Section counts

Mostly single-section (685 of 835), but a long tail up to 38 sections. Must
handle arbitrary section_count — records/strings/id_list/copy_table/
relationship/offset_map are all PER-section.

## Header flags (WDC3 uint16)

- `0x0`: 104 files
- `0x4`: 727 files — **flag 0x04 = non-inline IDs** (the id is in the per-section
  id_list, not in the record). The COMMON case here.
- `0x5`: 4 files — `0x04 | 0x01`, i.e. non-inline id + **sparse/offset-map**
  (flag 0x01). Sparse offset-map tables are RARE (4 files) but must work.
- No `0x2` at header level — relationship data is a per-section block, keyed by
  the DBD `$relation$` column, not a header flag in WDC3.

## Field storage compression kinds (field_storage_info.storage_type)

All six appear across the corpus — the full decode path is required:
- kind 0 none (751), 1 bitpacked (1178), 2 common_data (184),
  3 pallet (1353), 4 pallet-array (224), 5 bitpacked-signed (1276).

## Encryption — big deal

**138 files carry encrypted sections; 1348 encrypted sections total** (a
section with `tact_key_hash != 0`). Encrypted tables include heavy hitters:
item, itemsparse, spellname, map, areatable, broadcasttext, uimap,
contenttuning, transmogset, vehicle, battlepet*. The WoWCircle repack lacks the
TACT keys, so those sections' records arrive zeroed/garbage.
- **Policy (plan): PRESERVE the raw encrypted section bytes verbatim + REPORT**
  (encrypted_sections()/encrypted_ids(), DBCD-style); EXCLUDE their records from
  the decoded `records` vector. Never decode zeroed rows as real data.
- The corpus decode sweep must therefore tolerate encrypted sections as
  expected-absent rows, not errors. Byte-perfect round-trip is impossible for
  WDC3; validation is (a) structural-invariant decode sweep + (b) known-truth
  spot checks on UNencrypted tables.

## Implications for the WDC3 reader

- Parse: header → section_headers[N] → field_structure[field_count] (int16 size,
  uint16 position) → field_storage_info[field_storage_info_size/24] →
  pallet_data → common_data → then per section: records (inline, bit-addressed) /
  strings / id_list / copy_table / offset_map(+id list) / relationship block.
- Non-inline id (flag 0x04) comes from id_list[record]; sparse tables (0x01)
  use the offset_map instead of fixed stride and inline null-terminated strings.
- WDC2+ **string offsets are relative to the field's own position** — the
  single most error-prone item; the client concatenates records‖strings per
  section. Implement the documented back-conversion.
- Schema mapping: dbdgen must emit shadowlands-era records; the DBD marks
  `$noninline,id$` / `$relation$` so the engine knows which columns come from
  satellites vs inline fields. Inline DBD columns must align 1:1 with the file's
  field_structure order.
