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

## Not done

- **No sdist is published.** It would not build for anyone without gcc-16, so
  it would be provenance only.
- **No Linux arm64 / musl wheels**, and no `osx-x64` Python build older than
  the macos-13 image.
- **Nothing verifies the published artifacts** after upload (no install-from-
  PyPI check).
