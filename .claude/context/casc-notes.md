# CASC storage specifics

Read when: touching `fs/casc/casc_storage.*` or debugging CASC-era client access.

## Opening local storages (verified against WoWCircle 9.2.7, 2026-07)
- Retail installs: `CascOpenStorageEx` with `szLocalPath` = client root (CascLib
  finds `.build.info` itself), `szCodeName` = product ("wow").
- **Repacks without `.build.info`** (WoWCircle): plain opens fail with err 2. The
  working fallback, wired into `CascStorage::open`:
  1. scan `Data/config/xx/yy/*` for files whose first line is
     `# Build Configuration`; parse `build-name = WOW-<build>...`;
  2. for each candidate (requested-build match first, then build desc), write a
     shim dir under `$TMPDIR/wowlib-casc-shim/<hash>-<key>/` containing a
     synthetic `.build.info` and a `Data` **symlink** back into the client
     (client stays untouched), and try `CascOpenStorageEx(szLocalPath=shim)`;
  3. first success wins. Only actually opening reveals the right config —
     WoWCircle carries TWO 45745 build configs; `02788fc8...` fails
     ERROR_FILE_CORRUPT(1004), `43b2762b...` opens (2.17M files).
- Minimal synthetic `.build.info` CascLib parses:
  `Active!DEC:1|Build Key!HEX:16|CDN Key!HEX:16|Product!STRING:0` — the CDN key is
  required syntactically but unused for local storage (build key doubles as a
  stand-in). `CheckCascBuildFileExact` also accepts a path pointing directly at
  any `*.build.info`-suffixed file, but the storage root derives from its parent
  — hence the shim + symlink.
- Windows caveat (future): `create_directory_symlink` may need privileges;
  fallback would be junctions or writing `.build.info` into the client root.

## Reading
- FDID is the primary address: `CascOpenFile(CASC_FILE_DATA_ID(id), locale_mask,
  CASC_OPEN_BY_FILEID)` → `CascGetFileSize64` → one `CascReadFile`.
- Name lookups (`CASC_OPEN_BY_NAME`) only work when the root manifest carries
  name hashes (pre-8.2); on 9.2.7 → resolve through the listfile.
- **Repacks may ship partial data**: a file can OPEN but have size 0 and fail
  reads with err 1007 — its content isn't local yet (WoWCircle streams from CDN,
  so the on-disk set grows over time). Historical note: `map.db2` (fdid 1349477)
  read size-0 in 2026-07; **re-verified 2026-07-19 it now reads (120937 B, WDC3)**
  — that repack's local content had filled in. So don't hardcode a specific fdid
  as "not shipped"; the mechanism is real but which files are missing drifts.
  Stable test target that has always been local: fdid 1375801
  (`dbfilesclient/manifestinterfacedata.db2`, WDC3).
- `ERROR_FILE_ENCRYPTED` (1005) → wowlib `ErrorCode::EncryptedContent` (unknown
  TACT key).

## Error codes (CascPort.h, non-Windows)
2=not found, 1000=bad format, 1004=file corrupt, 1005=encrypted, 1007=overflow
(also observed for content-not-local reads).