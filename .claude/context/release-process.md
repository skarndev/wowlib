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

1. **PyPI Trusted Publishing** (no secret to store): on PyPI → the `wowlib`
   project → Publishing → add a GitHub publisher with owner `skarndev`, repo
   `wowlib`, workflow `release.yml`, environment `release`. Must be added
   before the first tag; the workflow's `id-token: write` is already set.
2. **`NUGET_API_KEY`** repo secret, from a nuget.org API key scoped to push
   `Wowlib`. The package ID is unclaimed until the first push.
3. A `release` GitHub **environment** — referenced by both publish jobs, and
   the place to add required reviewers if a release should need approval.

## Not done

- **No sdist is published.** It would not build for anyone without gcc-16, so
  it would be provenance only.
- **No Linux arm64 / musl wheels**, and no `osx-x64` Python build older than
  the macos-13 image.
- **Nothing verifies the published artifacts** after upload (no install-from-
  PyPI check).
