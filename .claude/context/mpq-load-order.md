# MPQ archive load order & chain tables

Read when: touching `fs/mpq/mpq_chain.*` or adding support for another MPQ-era
client version.

## The model
The client hardcodes its base archive list in the binary and wildcard-loads
patches after it; a later-loaded archive overrides earlier ones file-by-file.
wowlib encodes this as data-driven per-version tables (`ChainEntry` rows in
`mpq_chain.cpp`), expanded against the real `Data/` dir by `expand_chain` (pure,
StormLib-free, unit-tested with fake trees). Missing archives are skipped —
clients routinely lack optional patches. `MpqStorage` then opens every archive
standalone and searches in REVERSE chain order (last loaded wins).

## Verified 3.3.5a (build 12340) order
1. `common.MPQ`, `common-2.MPQ`, `expansion.MPQ`, `lichking.MPQ`
2. `{loc}/locale-{loc}.MPQ`, `speech`, `expansion-locale`, `expansion-speech`,
   `lichking-locale`, `lichking-speech`
3. `patch.MPQ`, `patch-2.MPQ`, `patch-3.MPQ`
4. `{loc}/patch-{loc}.MPQ`, `-2`, `-3`
5. custom `patch-{4..9}.MPQ` then `patch-{A..Z}.MPQ` (community convention;
   numbers before letters — ASCII '9' < 'A')
6. `{loc}/patch-{loc}-{4..9,A..Z}.MPQ`

`{loc}` auto-detected by scanning for `{code}/locale-{code}.MPQ`; ambiguous
(multi-locale repack) → caller must pass one. Distractors to skip: `base-{loc}`,
`backup-{loc}` archives.

Note: pywowlib sorted ALL `Data/*.MPQ` alphabetically — approximately right for
3.3.5a, wrong in general; don't copy that.

## Adding other versions
- 1.12 / 2.4.3: new `Fixed`/`NumberedSeq` tables only (dbc/model/... .MPQ for
  vanilla; common.MPQ + expansion.MPQ for TBC).
- 4.3.4 / 5.4.8: need `ChainEntryKind::UpdateChain` (`wow-update-{build}.MPQ`,
  ascending build) implemented, with `incremental_patch=true` → those archives
  must attach via `SFileOpenPatchArchive` on the base handles instead of opening
  standalone. Currently `expand_chain` returns NotImplemented for UpdateChain.