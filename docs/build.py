#!/usr/bin/env python3
"""Build (or serve) the wowlib documentation site.

One site, two toolchains, cleanly separated — the same split welder uses:

  * **Doxygen** renders the full C++ reference from the real headers under
    ``src/wowlib/`` (through welder's own ``INPUT_FILTER``, so the
    ``[[=welder::doc/returns/tparam]]`` annotations become Doxygen comments),
    themed with doxygen-awesome-css, into an out-of-source dir under ``build/``.
  * **mkdocs-material** renders the hand-written guide *and* the Python API
    reference (mkdocstrings over the generated ``.pyi`` stub tree). The
    ``inject_reference`` hook grafts the Doxygen HTML into ``<site>/api`` after
    every build/serve, so the reference never has to sit inside ``docs_dir`` and
    the worktree stays clean.

This script is the portable stand-in for welder's ``docs/CMakeLists.txt``: it
runs from the project ``.venv`` (which carries mkdocs-material, mkdocstrings and
``lark`` — the last for the Doxygen filter). Nothing here needs a configured
CMake tree *except* two inputs it locates for you:

  * welder's Doxygen filter (``$WOWLIB_WELDER_FILTER`` override → the pinned
    FetchContent checkout under ``build/`` → a sibling ``../welder`` checkout);
  * the ``.pyi`` stub tree for the Python API (built by the ``wowlib_pyi``
    target). Missing → the C++ reference and guide still build; the Python API
    pages just render empty (a warning is printed).

Usage::

    .venv/bin/python docs/build.py build            # -> build/docs/site/index.html
    .venv/bin/python docs/build.py serve            # live-reload the guide + Python API
    .venv/bin/python docs/build.py doxygen          # just the C++ reference (debugging)
    .venv/bin/python docs/build.py refresh-anchors  # refresh the wowdev FourCC->anchor maps

All generated artifacts land under ``build/docs/`` (gitignored). Re-run to
refresh; the Doxygen output dir is wiped each time so it can never accumulate
orphaned pages from renamed/removed symbols.
"""

from __future__ import annotations

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

DOCS_DIR = Path(__file__).resolve().parent
REPO_ROOT = DOCS_DIR.parent
BUILD_DIR = REPO_ROOT / "build" / "docs"

AWESOME_VERSION = "v2.4.2"
STUBS_REL = "build/bindings/bindings/python/stubs"

# The generic fields pages link each chunk FourCC to its section on wowdev.wiki.
# The anchors aren't bare FourCCs (MediaWiki spells them "MOHD_chunk",
# "MOSI_(optional)", …), so `refresh-anchors` pulls them from the API into vendored
# per-format maps, keeping the docs build itself offline. wowdev sits behind
# Cloudflare; a browser UA gets the JSON API through. Format key -> (wiki page,
# vendored map) — must stay in sync with the format configs' anchors_file.
WOWDEV_API = ("https://wowdev.wiki/api.php?action=parse&page={page}"
              "&prop=sections&format=json")
WOWDEV_ANCHOR_SOURCES = {
    "wmo": ("WMO", DOCS_DIR / "wmo_wowdev_anchors.json"),
    "m2": ("M2", DOCS_DIR / "m2_wowdev_anchors.json"),
}
BROWSER_UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/120 Safari/537.36")


def log(msg: str) -> None:
    print(f"wowlib-docs: {msg}", flush=True)


def die(msg: str) -> "None":
    sys.stderr.write(f"wowlib-docs: error: {msg}\n")
    raise SystemExit(1)


def project_version() -> str:
    try:
        with open(REPO_ROOT / "pyproject.toml", "rb") as f:
            return tomllib.load(f)["project"]["version"]
    except (OSError, KeyError, tomllib.TOMLDecodeError):
        return "0.0.0"


def find_welder_filter() -> Path:
    """Locate welder's Doxygen INPUT_FILTER (and its sibling .lark grammar).

    Prefer the pinned FetchContent checkout the library is actually welded
    against, so the filter matches the annotations in our sources; fall back to a
    sibling dev checkout. An explicit override always wins.
    """
    override = os.environ.get("WOWLIB_WELDER_FILTER")
    candidates: list[Path] = []
    if override:
        candidates.append(Path(override))
    # Pinned FetchContent checkouts (build/<preset>/_deps/welder-src/...).
    candidates += [
        Path(p)
        for p in sorted(
            glob.glob(
                str(REPO_ROOT / "build" / "*" / "_deps" / "welder-src"
                    / "tools" / "welder_doxygen_filter.py")
            )
        )
    ]
    # Sibling dev checkout.
    candidates.append(REPO_ROOT.parent / "welder" / "tools" / "welder_doxygen_filter.py")
    for c in candidates:
        if c.is_file() and c.with_suffix(".lark").is_file():
            return c
    die(
        "could not find welder's Doxygen filter (welder_doxygen_filter.py + .lark).\n"
        "  Configure a build (populates build/*/_deps/welder-src), or set\n"
        "  WOWLIB_WELDER_FILTER=/path/to/welder/tools/welder_doxygen_filter.py"
    )


def fetch_doxygen_awesome() -> Path | None:
    """Clone doxygen-awesome-css (the modern theme). Optional: on any failure we
    degrade to Doxygen's stock theme rather than aborting. The version is in the
    dir name so a bump re-clones into a fresh directory."""
    dest = BUILD_DIR / f"doxygen-awesome-css-{AWESOME_VERSION}"
    if (dest / "doxygen-awesome.css").is_file():
        return dest
    if not shutil.which("git"):
        log("git not found; using Doxygen's stock theme")
        return None
    log(f"fetching doxygen-awesome-css {AWESOME_VERSION}...")
    res = subprocess.run(
        ["git", "clone", "--depth", "1", "--branch", AWESOME_VERSION,
         "https://github.com/jothepro/doxygen-awesome-css.git", str(dest)],
        capture_output=True, text=True,
    )
    if res.returncode != 0 or not (dest / "doxygen-awesome.css").is_file():
        log("could not fetch doxygen-awesome-css; using stock theme")
        return None
    return dest


def build_doxygen_header(doxygen: str) -> Path | None:
    """Generate Doxygen's default header, then inject our self-contained dark-mode
    toggle + guide backlink (Doxygen 1.17 ships no jQuery, so doxygen-awesome's own
    JS never runs). Any failure -> stock header (still the base theme)."""
    gen = BUILD_DIR / "header.generated.html"
    res = subprocess.run(
        [doxygen, "-w", "html", str(gen),
         str(BUILD_DIR / "footer.generated.html"),
         str(BUILD_DIR / "style.generated.css")],
        cwd=BUILD_DIR, capture_output=True, text=True,
    )
    if res.returncode != 0 or not gen.is_file():
        return None
    out = BUILD_DIR / "header.html"
    res = subprocess.run(
        [sys.executable, str(DOCS_DIR / "patch_doxygen_header.py"), str(gen), str(out)],
    )
    return out if res.returncode == 0 and out.is_file() else None


def configure_doxyfile(*, awesome: Path | None, header: Path | None,
                       have_dot: str) -> Path:
    filter_py = find_welder_filter()
    ref_dir = BUILD_DIR / "reference"          # OUTPUT_DIRECTORY; HTML_OUTPUT = api
    stylesheets = ""
    extra_files = ""
    if awesome:
        stylesheets = f"{awesome / 'doxygen-awesome.css'} {DOCS_DIR / 'doxygen-extra.css'}"
    subs = {
        "PROJECT_VERSION": project_version(),
        "PROJECT_LOGO": str(DOCS_DIR / "wowlib-logo.svg"),
        "DOXYGEN_OUTPUT_DIR": str(ref_dir),
        "DOXYGEN_TAGFILE": str(ref_dir / "wowlib.tag"),
        "WOWLIB_SRC": str(REPO_ROOT / "src" / "wowlib"),
        "DOXYGEN_MAINPAGE": str(DOCS_DIR / "api_mainpage.md"),
        "STRIP_ROOT": str(REPO_ROOT),
        "STRIP_INC": str(REPO_ROOT / "src"),
        "FILTER_PYTHON": sys.executable,
        "FILTER": str(filter_py),
        "HAVE_DOT": have_dot,
        "HTML_HEADER": str(header) if header else "",
        "HTML_EXTRA_STYLESHEET": stylesheets,
        "HTML_EXTRA_FILES": extra_files,
    }
    text = (DOCS_DIR / "Doxyfile.in").read_text(encoding="utf-8")
    for key, val in subs.items():
        text = text.replace(f"@{key}@", val)
    out = BUILD_DIR / "Doxyfile"
    out.write_text(text, encoding="utf-8")
    return out


def run_doxygen() -> Path:
    doxygen = shutil.which("doxygen")
    if not doxygen:
        die("'doxygen' not found on PATH")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    awesome = fetch_doxygen_awesome()
    header = build_doxygen_header(doxygen) if awesome else None
    have_dot = "YES" if shutil.which("dot") else "NO"
    doxyfile = configure_doxyfile(awesome=awesome, header=header, have_dot=have_dot)

    ref_dir = BUILD_DIR / "reference"
    shutil.rmtree(ref_dir, ignore_errors=True)      # never accumulate orphaned pages
    log(f"building Doxygen C++ reference -> {ref_dir / 'api'}")
    res = subprocess.run([doxygen, str(doxyfile)], cwd=BUILD_DIR)
    if res.returncode != 0:
        die("doxygen failed")
    return ref_dir / "api"


def check_stubs() -> None:
    if not (REPO_ROOT / STUBS_REL / "wowlib" / "__init__.pyi").is_file():
        log(f"WARNING: Python stubs not found under {STUBS_REL}; the Python API "
            "pages will be empty. Build the 'wowlib_pyi' target first.")


def run_mkdocs(action: str, api_dir: Path) -> None:
    check_stubs()
    env = dict(os.environ, WOWLIB_DOXYGEN_API=str(api_dir))
    site_dir = BUILD_DIR / "site"
    cmd = [sys.executable, "-m", "mkdocs", action,
           "--config-file", str(DOCS_DIR / "mkdocs.yml")]
    if action == "build":
        cmd += ["--site-dir", str(site_dir), "--clean"]
        log(f"building mkdocs site (guide + Python API + reference) -> {site_dir}")
    else:
        # Watch the reloadable hook logic + its data so serve reflects edits live
        # (the format_reference.py shim reloads the engine + configs on each
        # rebuild).
        for extra in ("format_reference_impl.py", "wmo_reference_config.py",
                      "m2_reference_config.py", "common_reference_config.py",
                      "wmo_wowdev_anchors.json", "m2_wowdev_anchors.json"):
            cmd += ["--watch", str(DOCS_DIR / extra)]
        # Also watch the .pyi stub tree: the Python API is rendered from it, so a
        # bindings rebuild (regenerating stubs) must re-render — otherwise serve
        # keeps serving the previous stubs' classes/signatures.
        cmd += ["--watch", str(REPO_ROOT / STUBS_REL)]
        log("serving at http://127.0.0.1:8000/ (guide + Python API live-reload; "
            "the C++ reference is a snapshot)")
    res = subprocess.run(cmd, env=env, cwd=REPO_ROOT)
    if res.returncode != 0:
        die(f"mkdocs {action} failed")
    if action == "build":
        log(f"done -> {site_dir / 'index.html'}")


def refresh_anchors() -> int:
    """Re-fetch the wowdev.wiki section anchors for every format and rewrite the
    vendored FourCC->anchor maps (docs/*_wowdev_anchors.json), then report each
    format's field coverage against its config."""
    import json
    import re
    import sys
    import urllib.request

    # Field FourCCs per format, from the format configs (the same parse the site
    # build uses, so coverage never drifts from what actually gets badged).
    sys.path.insert(0, str(DOCS_DIR))
    try:
        import format_reference_impl as fri
        fmts = {f.key: f for f in fri.formats()}
    except (OSError, ImportError) as e:
        log(f"(coverage checks will be skipped: {e})")
        fmts = {}

    for key, (page, dest) in WOWDEV_ANCHOR_SOURCES.items():
        log(f"fetching {page} section anchors from wowdev.wiki (MediaWiki API)...")
        req = urllib.request.Request(WOWDEV_API.format(page=page),
                                     headers={"User-Agent": BROWSER_UA})
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                sections = json.load(resp)["parse"]["sections"]
        except (OSError, KeyError, ValueError) as e:
            die(f"could not fetch wowdev.wiki {page} sections: {e}")

        # Map each section's leading FourCC to its anchor; first occurrence wins, so
        # a primary chunk beats a later same-code section (e.g. the v14 MOLV -> MOLV_2).
        amap: dict[str, str] = {}
        for s in sections:
            line = re.sub(r"<[^>]*>", "", s.get("line", ""))   # strip heading HTML
            m = re.match(r"\s*([A-Z][A-Z0-9]{3})\b", line)
            if m and m.group(1) not in amap:
                amap[m.group(1)] = s["anchor"]
        if not amap:
            die(f"no section anchors parsed — wowdev's {page} page structure may "
                "have changed")
        dest.write_text(json.dumps(amap, indent=0, sort_keys=True) + "\n",
                        encoding="utf-8")
        log(f"wrote {dest.relative_to(REPO_ROOT)} ({len(amap)} anchors)")

        # Coverage: warn if any badged field FourCC lacks an anchor (would link to
        # the page top).
        fmt = fmts.get(key)
        if fmt is None:
            continue
        try:
            ours = {f["cc"] for side in fmt.sides for f in side.fields().values()
                    if re.fullmatch(r"[A-Z0-9]{4}", f["cc"] or "")}
        except OSError as e:
            log(f"(coverage check skipped for {key}: {e})")
            continue
        missing = sorted(c for c in ours if c not in amap)
        if missing:
            log(f"WARNING: {len(missing)} {key} field FourCC(s) have no anchor "
                f"(will link to the {page} page top): {', '.join(missing)}")
        else:
            log(f"all {len(ours)} {key} field FourCCs resolve to a section anchor")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Build the wowlib documentation site.")
    ap.add_argument("action", nargs="?", default="build",
                    choices=["build", "serve", "doxygen", "refresh-anchors"],
                    help="build the full site (default), serve it, just Doxygen, or "
                         "refresh the per-format wowdev.wiki FourCC->anchor maps")
    args = ap.parse_args()

    if args.action == "refresh-anchors":
        return refresh_anchors()
    if args.action == "doxygen":
        run_doxygen()
        return 0
    api_dir = run_doxygen()
    run_mkdocs(args.action, api_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
