# Releasing (PyPI + NuGet)

Read when: cutting a release, touching `.github/workflows/release.yml`, or
changing how versions are derived.

## Cutting one

```bash
git tag v0.1.0 && git push origin v0.1.0
```

That is the whole procedure — **no version is written anywhere in the tree**.
`release.yml` builds four wheels and a four-RID NuGet package, then publishes
both. A tag whose commit is not an ancestor of `origin/main` is refused by the
`guard` job before anything builds (a tag is not "on" a branch, so containment
is checked explicitly).

**Rehearse first with `workflow_dispatch`** (optionally passing a ref): it runs
the entire matrix and packs everything with `publish=false`, so the artifacts
are downloadable from the run but nothing reaches a registry. The publish steps
are the only irreversible part.

## Package names

| Registry | Name | Note |
|---|---|---|
| PyPI | **`wowlib-py`** | `wowlib` is taken by an unrelated Excel-conversion toolkit (another studio's internal tool, last release 2018-10-31) that also ships a top-level `wowlib` package, so the two cannot coexist in one environment. Ours is the only one anyone in this ecosystem wants, but the name is not available. |
| import | **`wowlib`** | Unchanged, and independent of the distribution name (pillow → PIL). Nothing in the docs, stubs or examples had to change. |
| NuGet | **`Wowlib`** | Free and claimed by our first push. |

If the PyPI name is ever wanted, PEP 541 allows requesting an abandoned
project's name — 8 years without a release is the strongest ground PyPI
recognises — but it is slow and not guaranteed, so `wowlib-py` is the name to
publish under meanwhile.

## Versioning: the tag is the only source

- **Python**: `[project] dynamic = ["version"]` +
  `[[tool.dynamic-metadata]] provider = "scikit_build_core.metadata.setuptools_scm"`.
  scikit-build-core's setuptools-scm plugin is the backend's equivalent of
  hatch-vcs (hatch-vcs itself is a *hatchling* plugin and does not apply here).
  Verified: clean tree on `v9.9.9` → `9.9.9`; untagged → `0.0.post1.dev194`.
  - Needs `fetch-depth: 0` — without tags in the checkout the version is wrong,
    not merely missing.
  - A **dirty** tree yields `<tag>.post1.dev0`, so a release build must be a
    clean checkout.
  - `local_scheme = "no-local-version"`: setuptools-scm would otherwise append
    `+g<sha>` to any non-tag build, and PyPI rejects local versions — this way
    an accidental non-tag build is merely wrong-numbered, not
    unpublishable-at-the-last-step.
  - A tag that is not PEP 440-parseable (`v9.9.9-probe`) fails the build loudly.
    Use `vMAJOR.MINOR.PATCH`.
- **C#**: the same string is passed to CMake as
  `-DWOWLIB_CSHARP_PACKAGE_VERSION`, so both artifacts of a release always
  carry one number.

## Why not cibuildwheel

The manylinux images ship no compiler with C++26 reflection, and gcc-16 is the
only toolchain that has it. Each wheel leg therefore installs the toolchain the
way the matching `ci-*.yml` does (Homebrew; MSYS2/UCRT64 on Windows) and builds
directly. Consequences:

- Every wheel links the C++ runtime **statically**
  (`-static-libstdc++ -static-libgcc` via `SKBUILD_CMAKE_DEFINE`) — it installs
  on machines with no Homebrew/MSYS2 gcc.
- Linux wheels go through `auditwheel repair`: a `linux_x86_64` tag is rejected
  by PyPI outright, and the repair retags to the manylinux level the build
  actually needs (with the runtime static there is usually nothing to bundle).
  macOS goes through `delocate` for the same reason.
- The manylinux floor is whatever glibc `ubuntu-latest` carries — recent. Users
  on older distros cannot install the wheel; that is inherent to the toolchain
  requirement, not something the workflow chose.
- Each wheel is smoke-tested by installing it into a fresh venv and importing
  it, so a wheel that builds but cannot load never ships.
- Windows builds against the **MSVC** CPython (setup-python), not MSYS2's, so
  the `.pyd` links `python3.dll` (stable ABI) and loads in a stock install.
- The wheel binds only the four eras in `[tool.scikit-build.cmake.define]`
  (`WOWLIB_DB_ERAS`), not all eleven — the same bound as the local bindings
  build.

## Build time: generate once, compile everywhere

The C# generator (`gen.cpp`) is ONE reflection-heavy translation unit that
reflects the whole surface — every format across the version matrix plus 1221
DB tables across 11 eras. Measured locally: **2388 s (40 min), serial**, versus
~460 s for the worst shim shard and **5 s** for the shared prologue every shard
re-parses. So the shard split and PCH are not where the time is; the generator
is.

Its output is platform-independent text, so `release.yml` runs it exactly once:

- **`csharp-generate`** (Linux) builds and runs the generator, then uploads
  `generated-sources` (`shim.*.cpp` + `Bindings.*.cs`), `managed-wrapper` and
  its own `native-linux-x64`.
- **`csharp-native`** (macOS, Windows) downloads that artifact and configures
  with `WOWLIB_CS_PREGENERATED_DIR`, which routes to welder-csharp's
  `PREGENERATED_DIR` — no generator target is even added to the build graph
  (verified: 0 generator targets, 32 shim objects).

The `SHARDS`/`CS_FILES` counts must match the generating run, or the consuming
configure fails with a named missing file rather than something cryptic.

## The NuGet package is assembled in CI, not by CMake

`welder_csharp_nuget_project` can only ever know the platform it ran on, so its
csproj is right for a local pack and wrong for a release. The `nuget` job
writes its own project that compiles the managed wrapper **once** (the
generated C# is a reflection of the C++ surface, so it is platform-independent
— taken from the Linux leg) and packs all four native libraries under
`runtimes/<rid>/native/`. Rehearsed locally: layout verified with real
`Bindings.*.cs` + a real dylib and three stand-ins, and the job's own
verification step asserts the assembly, the XML doc sidecar and all four RIDs
are present — a silently RID-less package would install fine and fail at the
first P/Invoke.

## One-time setup (outside the repo)

**Neither registry stores a long-lived key** — both publish over OIDC, so the
repo holds no push credential at all.

1. **PyPI Trusted Publishing**: PyPI → Publishing → add a *pending* GitHub
   publisher (pending, because the project does not exist there until the first
   upload) with project `wowlib-py`, owner `skarndev`, repo `wowlib`, workflow
   `release.yml`, environment `release`.
2. **nuget.org Trusted Publishing**: nuget.org → your username → Trusted
   Publishing → add a policy with Repository Owner `skarndev`, Repository
   `wowlib`, Workflow File `release.yml` (**file name only**, no
   `.github/workflows/` prefix), Environment `release`. The workflow exchanges
   the OIDC token via `NuGet/login@v1` for a key valid **one hour**, single-use,
   requested immediately before the push.
   - A policy can start **temporarily active for 7 days** (this happens for
     private repos — ours is public, so it should activate outright). If it does
     land in that state, a successful publish within the window makes it
     permanent; the window is restartable.
   - The policy is owned by a user or an org, and goes inactive if that
     ownership lapses (e.g. the creator leaves the org).
3. **`NUGET_USER`** repo variable/secret: the nuget.org **profile name**, not an
   email. Not a credential — it identifies which account's policy to match — but
   kept out of the workflow text per NuGet's guidance.
4. A `release` GitHub **environment** — referenced by both publish jobs, matched
   by both policies, and the place to add required reviewers if a release should
   need approval. If you drop it here, drop it from both policies too, or the
   token claims stop matching.

## A burned version cannot be reused — plan around it

**PyPI reserves filenames permanently, even after you delete the release.** A
re-upload gets *"This filename has already been used, use a different
version"*; the guarantee is that a given file always resolves to the same
bytes. Deleting a botched release therefore frees NOTHING — the next attempt
must bump the version. `skip-existing` does not help here either: it covers
files present in the index, not reserved-but-deleted names. NuGet is the same
in spirit (unlist, never overwrite).

Practical consequence: **0.0.1 is spent on PyPI** (published 2026-08-15 with
Linux + macOS wheels, then deleted), so the first complete release is 0.0.2.

## The two registries release together

Both publish jobs need `[guard, wheels, nuget]`, so neither registry gets a
version the other did not. This was NOT the original shape: `publish-pypi`
needed only `wheels`, which is how 0.0.1 went out to PyPI off a run whose C#
matrix had already failed.

Two registries still cannot be committed atomically. If one push fails after
the other succeeded, **re-run that single job** — the artifacts persist on the
run and carry the same version, so it republishes identically. Do not bump and
re-tag for that case: the versions would then disagree, which is the thing this
gating exists to prevent.

## Gotchas the first run found (2026-08-14, v0.0.1)

- **The workflow is read from the TAG, not from main.** A fix pushed to main
  does nothing for a tag already pushed — the tag has to be moved (or a new one
  cut). Moving it is only safe while nothing has published; once PyPI or NuGet
  has accepted the version it can never be reused, even after deletion, and the
  next attempt must bump the number.
- **On Windows the job shell is MSYS2, which has no `python`.** The wheel is
  built against the MSVC CPython from setup-python (so the `.pyd` links
  `python3.dll` and loads in a stock install), and MSYS2's own python is
  deliberately not installed in the wheels job. Resolve the interpreter with
  `cygpath -m "$pythonLocation/python.exe"` BEFORE any use — a bare
  `python -m pip …` fails with `command not found` (exit 127). The smoke-test
  step is exempt: it sets `shell: bash` (Git Bash), where setup-python's
  interpreter is on PATH.
- **The version must be PINNED for the build, not re-derived per runner.**
  setuptools-scm reads the WORKTREE, and on Windows the checkout is made by
  Windows Git while the build runs under MSYS2's git; their CRLF handling
  differs, tracked files look modified, and a dirty tree yields
  `<tag>.post1.dev0`. 0.0.1's Windows wheel was published to PyPI under that
  version, as a separate release, while Linux and macOS published 0.0.1.
  `SETUPTOOLS_SCM_PRETEND_VERSION` (fed from `guard`, which derives it from the
  tag) removes the whole class of problem — shallow clones and missing tags
  included.
- **A retired runner image does not fail — it QUEUES.** `macos-13` was removed,
  and jobs targeting it sat `queued` indefinitely while every other leg ran;
  they would have burned the 240-minute timeout before failing and blocking the
  publish. If a leg is still `queued` while its siblings are `in_progress`,
  suspect the image label, not capacity. The Intel label is now
  `macos-26-intel` (standard runner, free for public repos); `macos-15-intel`
  is the other current option.
- **`SKBUILD_CMAKE_DEFINE` did not reach CMake.** The static-C++-runtime flag
  set that way was silently ignored, and the Linux wheel came out dynamically
  linked against `libstdc++.so.6` — which is ALSO what made `auditwheel repair`
  fail ("too-recent versioned symbols": the bundled libstdc++ carried
  `GLIBCXX_3.4.35`). Pass build-affecting defines as
  `-C cmake.define.<NAME>=<value>` on the `python -m build` command line, the
  same shape `ci-linux.yml` uses as a plain `-D`. A dedicated step now asserts
  (ldd/otool/objdump) that no dynamic C++ runtime dependency survives, so the
  failure surfaces where the cause is legible.
- **A Python extension is a `MODULE` library, not `SHARED`.** So
  `CMAKE_SHARED_LINKER_FLAGS` never reaches its link line — the static-runtime
  flag must go to `CMAKE_MODULE_LINKER_FLAGS`. This is why the wheel kept
  linking `libstdc++.so.6` and why auditwheel then refused it: the manylinux
  policy caps `GLIBCXX` at 3.4.9 and `GCC` at 7.0.0 (verified against
  auditwheel 6.8's manylinux_2_39 policy), while a gcc-16 build needs
  GLIBCXX_3.4.35 / GCC_13.0.0. glibc was never the problem. **`ci-linux.yml`
  passes the same flag as `CMAKE_SHARED_LINKER_FLAGS` for the bindings job, so
  its "static libstdc++" claim is probably also untrue — worth re-checking with
  `ldd`.**
- **`! cmd` is exempt from `set -e`.** An assertion written as
  `! ldd "$mod" | grep -q libstdc++` PASSES even when the grep matches, and an
  empty `$mod` makes it pass vacuously too. Write assertions as
  `if <bad>; then echo "::error::…"; exit 1; fi` and check the input is
  non-empty first.
- **The C# native job OOMs the runner at the default pool size.** Exit 143 /
  "the runner has received a shutdown signal" on ubuntu-latest: the adaptive
  `WOWLIB_PY_COMPILE_JOBS` (RAM / 3.5 GB → 4 on a 16 GB runner) is too generous
  for shim shards that reflect the whole surface AND every ClientDB table. The
  release pins it to 2.
- **The C# native build is SLOW on the macOS runner** — it hit the 240-minute
  job timeout and was cancelled mid-build (0.0.1). That runner is 3 cores /
  7 GB and compiles 32 shim shards which each re-parse the whole welded
  surface; Windows, at twice the cores, took 155 minutes. The job budget is now
  350 minutes (GitHub's ceiling is 360). If it ever times out again, lowering
  `WOWLIB_CS_SHARDS` for CI is the lever with real headroom: fewer shards means
  less redundant parsing overall, at the cost of more memory per TU — which
  that 7 GB runner can least afford, so measure rather than guess.
- **`msys2/setup-msys2` can fail with HTTP 429.** Both Windows jobs start
  together and race on the same download; the action retries twice and then
  fails the job. It is transient and not a workflow defect — re-run the failed
  job (successful legs' artifacts are reused, so it does not cost another full
  matrix).

## Not done

- **No sdist is published.** It would not build for anyone without gcc-16, so
  it would be provenance only.
- **No Intel macOS artifacts** (dropped in 0.0.1).
  `cmake/DarwinAtomProbe.cmake` — the configure gate for the gcc-16 Mach-O
  literal-atom miscompile (GCC PR 126723) — is hand-written **aarch64**
  assembly (`adrp`/`@PAGE`/`@PAGEOFF`) and does not assemble on x86_64, so the
  leg cannot configure. The probe must be PORTED, never skipped: the bug hits
  x86_64 too (confirmed under Rosetta), so an unguarded Intel wheel could
  corrupt string data silently. To restore, add an x86_64 probe variant
  (`leaq L.str.N(%rip)` in place of the adrp pair), arch-select it in the
  probe, then re-add `macos-26-intel` to both matrices and `osx-x64` to the
  NuGet project + its verification list.
- **No Linux arm64 / musl wheels.**
- **Nothing verifies the published artifacts** after upload (no install-from-
  PyPI check).
