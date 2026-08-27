# CLion (Nova) formatter setup

## Where the config lives
This project's CLion is **Nova** (ReSharper C++ engine). The formatter is driven
by `/Default/CodeStyle/CodeFormatting/CppFormatting/...` option entries, which
live in **two** places that the IDE keeps in sync:

- `.idea/codeStyles/Project.xml` — the shared scheme. **This is the one to
  edit.** The entries must be wrapped in a `<RiderCodeStyleSettings>` element;
  written that way it works. (An earlier attempt failed only because it used the
  IntelliJ-engine form — `<Objective-C>` / `<codeStyleSettings language="...">`
  blocks — which Nova ignores entirely. Same file, different dialect.)
  Requires `codeStyleConfig.xml` with `USE_PER_PROJECT_SETTINGS=true`.
- `.idea/editor.xml` — the IDE rewrites this to mirror the scheme.

`.editorconfig` is **off** by design: the scheme sets `<editorconfig><option
name="ENABLED" value="false"/>`. It also doesn't integrate with the Settings GUI
(user's call, 2026-08-25), so there is no `.editorconfig` in the repo.

Note `.idea/` is git-ignored, so the style is per-machine. To make it travel,
change `.gitignore` to `.idea/*` + `!.idea/codeStyles/`.

The running IDE does NOT hot-reload externally-written .idea files; edit them
through the IDE (MCP `apply_patch`/`create_new_file`) or have the user refocus /
restart CLion.

## Configured style (codeStyles/Project.xml, user-authored 2026-08-25)
- `INDENT_SIZE=2`, `AUTODETECT_INDENTS=false`
- Braces attached (`END_OF_LINE`) for namespace/export/type/invocable/anonymous-
  method/case-block/requires-expression/other
- `WRAP_LIMIT=120` (was 80; raised 2026-08-25 — see trailing comments below)
- `EMPTY_BLOCK_STYLE=TOGETHER_SAME_LINE` (`struct Foo {};`)
- `CHOP_IF_LONG` for base clauses, braced-init lists, ctor initializers, params
- `MAX_ENUM_MEMBERS_ON_LINE=1`, `KEEP_EXISTING_ENUM_ARRANGEMENT=**true**`
  (flipped from false — see trailing comments below)

Keep the two files in sync: patch `codeStyles/Project.xml` *and* `editor.xml`.

## Command line: inspections work, the format check does NOT
Both CLIs live in `/Applications/CLion.app/Contents/bin/` (use the **stable**
install — the 2026.2 EAP build is expired and refuses to run headless). They
launch a headless IDE, which collides with a running CLion unless you point it
at a private config dir:

```sh
cat > /tmp/clion-cli.properties <<'EOF'
idea.config.path=/tmp/clion-cli/config
idea.system.path=/tmp/clion-cli/system
idea.log.path=/tmp/clion-cli/log
idea.plugins.path=/tmp/clion-cli/plugins
EOF
export CLION_PROPERTIES=/tmp/clion-cli.properties
```

**`inspect.sh` works** and does run the ReSharper backend (the output includes
`RadGlobal.json`), so it reports the `Cpp*` inspections:
```sh
/Applications/CLion.app/Contents/bin/inspect.sh "$PWD" -e /tmp/inspect-out \
    -d src/wowlib/core -format json -v1
```
`-e` uses the project profile; `-d` scopes to a directory; `-changes` limits it
to uncommitted changes. One JSON file per inspection id. On `src/wowlib/core`
alone it reports 109 `CppInconsistentNaming` hits (e.g. `locale_table` →
"Suggested name is `LOCALE_TABLE`") — the naming rules in the scheme, as noted
below.

**`format.sh` is useless for C++ here — do not gate CI on it.** Nova delegates
C++ formatting to the ReSharper backend, which the headless IntelliJ formatter
never invokes, so `-d` (dry run) reports *"Formatted well"* unconditionally.
Verified by mangling a real, tracked, CMake-model file (Allman brace + 8-space
indent) — still "well formed", exit 0. There is no working CLI formatting check;
reformat through the IDE (or `mcp__clion__reformat_file`) and use
`git diff --stat` to see what moved.

## Re-flowing the whole tree to a new WRAP_LIMIT
`KEEP_USER_LINEBREAKS` defaults to **true**, so raising `WRAP_LIMIT` only
affects new code — existing wrapped lines stay wrapped. A real re-flow needs
`KEEP_USER_LINEBREAKS=false`, and that setting **destroys the annotation
convention** if run naked: it collapses the vertical `[[ =chunk(...), ... ]]`
lists onto one line, splits `=welder::doc` into `=` + newline + `welder::doc`,
drops the space after commas, and welds member declarations onto the `]]` line.

The working procedure (used 2026-08-25 for the 80 → 120 re-flow):
1. Fence every multi-line `[[...]]` block **and** every standalone one-line
   annotation with `// @formatter:off` / `// @formatter:on` (a script; bracket
   scan must skip raw and ordinary string literals, and count nesting — there
   are parameter-level annotations inside annotation blocks).
2. Set `KEEP_USER_LINEBREAKS=false`, reformat every file, restore the setting.
3. Strip the marker lines.
4. Verify: token-identical to HEAD, R-strings unchanged, `-fsyntax-only` on a
   few TUs.

**Nova bug to expect in step 3:** with `KEEP_USER_LINEBREAKS=false` the
formatter welds the following line onto a `// @formatter:on` line, producing
`// @formatter:onstd::uint32_t flags = 0;` — i.e. it comments the declaration
out. It hit 60 sites in 4 files. Strip markers with a regex that moves any
trailing text back onto its own line, never with a plain line-delete, and let
the token check confirm nothing was lost.

## Trailing `/**<` doc comments get relocated
Two independent rules move a trailing Doxygen comment onto its own line, where
it then reads as documenting the *next* member. `KEEP_USER_LINEBREAKS=true`
means the formatter never joins one back, so the damage is sticky and must be
repaired by hand/script:
- **struct/class members** — relocated when the line exceeds `WRAP_LIMIT`.
  Fixed by raising the limit to 120.
- **enum members** — relocated regardless of length by
  `MAX_ENUM_MEMBERS_ON_LINE=1`. Fixed by `KEEP_EXISTING_ENUM_ARRANGEMENT=true`.

The user's first sweep (at 80/false) displaced **145** of them across 20 files;
HEAD had zero. All were rejoined. Detect a recurrence with:
```sh
grep -rnE '^\s*/\*\*<' --include='*.hpp' --include='*.cpp' src
```
One site legitimately still splits: the `ClientFlavor::Classic` comment in
`core/client_version.hpp` is ~104 chars joined. Shorten the text to fix it.

## Annotation-list indent: +4, by decision
There is **no** ReSharper C++ option for attribute-list indent (the full option
vocabulary was dumped from `~/Library/Application Support/JetBrains/CLion*/
codestyles/Default.xml` and a full project `editor.xml`; nothing matches
`ATTRIBUTE`). `CONTINUOUS_LINE_INDENT` does *not* affect it — verified.
So `struct [[` annotation entries land at **+4** with the closing `]]` at +2,
where the hand-written style used +2/+0. The user decided (2026-08-25) to
**accept +4** rather than maintain a post-reformat fixer script or 500+ lines of
`// @formatter:off` markers. Do not "fix" this indentation by hand — a reformat
undoes it.

Two things that do NOT work, both verified, do not retry:
- `// @formatter:off` / `:on` *does* work and preserves +2 — rejected only for
  the line-noise of wrapping 259 blocks in 69 files.
- Moving the attribute before the class-key (`[[=x]] struct Foo`) formats at +2
  but **silently drops the annotations**: gcc-16 emits *"attribute ignored in
  declaration of 'struct Foo' … must follow the 'struct' keyword"* and
  `annotations_of` returns 0. It would un-weld the entire library.
- Include style: `UseRelativePaths=Never`, angle brackets `WhenPossible`
  (matches the `#include <wowlib/...>` convention)

## Whole-project sweep audit (2026-08-25, 148 files, +9519/-8920)
Verified **token-identical to HEAD in all 148 files** (compare with all
whitespace stripped) — the sweep changed only whitespace, no code semantics.

Re-run that check after any future sweep:
```sh
for f in $(git diff --name-only); do
  case "$f" in *.hpp|*.cpp|*.h) ;; *) continue;; esac
  a=$(git show HEAD:"$f" | tr -d '[:space:]' | shasum)
  b=$(tr -d '[:space:]' < "$f" | shasum)
  [ "$a" != "$b" ] && echo "TOKEN-DIFF: $f"
done
```
That check is blind to whitespace *inside* string literals, so also diff the
`R"(...)"` contents (a python `re.findall(r'R"\((.*?)\)"', s, re.S)` compare).
One hit: `formats/wmo/root/root.hpp` had its `welder::doc` body dedented 8→6
spaces. Harmless — welder's `cleandoc` (welder `src/welder/doc.hpp`) strips the
**common** indent PEP-257-style, and the shift was uniform. But it proves the
formatter can reach inside raw strings: a doc containing a deliberately indented
example block could have its rendered output changed. Check R-strings after
sweeps.

The enum-NTTP `< V >` bug below did **not** reproduce under this config.

## What survives reformat (verified on wdt/root/root.hpp)
- Vertical `[[=chunk(...), =since(...), ...]]` member annotation blocks:
  byte-identical.
- `R"(...)"` doc strings: untouched (token content).
- Verified-good file-level diffs: only brace attachment + intended changes.

Known cosmetic deviations (no knob found; accepted):
- struct-level `struct [[` annotation bodies indent to +4 (members stay +2);
  closing `]]` shifts to +2.
- Concatenated plain string literals ("..." "...") lose hand alignment →
  continuation indent. Prefer R-strings for multi-line docs.
- Wrapped `using X =` continuation loses its +2.

## Non-formatting rules in the scheme that fight the codebase
These are **inspection/cleanup** settings — "Reformat Code" does not apply them,
but they raise warnings everywhere and a **Code Cleanup run would rewrite code**.
Counts measured 2026-08-25:
- `BracesInIfStatement/ForStatement/WhileStatement=Required` vs **1014**
  brace-less single-statement bodies (`if (!r) return ...;` is the house style).
- Naming "Classes and structs" rule (`AaBb_AaBb`) includes `namespace` — but all
  **229** namespaces are lowercase (`wowlib::formats::wdt::root`).
- Naming "Global constants" rule (`AA_BB`) vs **136** lowercase
  `inline constexpr` constants — and it contradicts the documented
  `builds::Expansion_PatchTitle` convention.
- `FunctionDeclarationStyle=TrailingReturnType` vs ~**342** leading-return-type
  definitions.
- `SortDefinitions=true` — a cleanup would reorder .cpp definitions to match
  header order.
Either relax these to match the house style or never run Code Cleanup with them
enabled.

## clang-format: unusable
clang-format 21 token-mangles P2996/P3394 annotations: joins the vertical
`=...` list, rewrites `=welder::weld` → `= welder::weld`, `struct [[` →
`struct[[`, detaches `]]`. Do not introduce a `.clang-format`.
(`EnableClangFormatSupport=false` is set in editor.xml.)

## Nova formatter bug: enum-NTTP template-ids in dependent context
Reformatting a file that uses a **header-declared** template with an
**enum-class NTTP** and a **dependent** argument in statement context mangles
the template-id: `m2::M2<V> model;` → `m2::M2 < V > model;`. Annotations are
irrelevant (plain structs repro); same-file declarations and concrete args
(`Foo<ProbeVersion::a>`) are fine. clangd shows no errors — it's the ReSharper
formatting parser only. Hits every `Entity<V>` local in `template
<ClientVersion V>` driver code (audit/*.cpp, converters). After any whole-file
reformat of such files, check the diff for ` < V > ` and fix manually. Worth
reporting to JetBrains (youtrack, CPP project).

## Naming rules (updated 2026-08-27, sweep applied)
The naming rules now flag camelCase methods/params/locals/fields, `_camelCase`
private members (methods included), Pascal enumerators and constants. The
`k` constant prefix was removed from the scheme by user decision (WarnAbout-
PrefixesAndSuffixes=False does NOT disable a configured prefix — an already-
AaBb name like `Cata` was still flagged for the missing `k`). The whole tree
was renamed to match — see `.claude/context/naming-convention.md` for the
convention, the canonical-spelling exemptions that will keep warning forever
(no abbreviation list exists in ReSharper C++), and the protocol-name traps.
