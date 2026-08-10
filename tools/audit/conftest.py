"""Options, fixtures and reporting glue for the exhaustive round-trip audit.

The audit is a pytest-driven sweep over real client installs: every test opens
one client through the wowlib Python bindings, enumerates its files, and calls
``wowlib.audit.Auditor.roundtrip`` on each — accumulating outcomes into
per-client x per-format CSVs plus one summary JSON. Round-trip failures are
DATA in the report, not test failures; a test only fails when the tool layer
itself breaks (the client will not open, enumeration errors out).

Run it explicitly (it is not part of the bindings-contract suite)::

    pytest tools/audit --clients-dir ~/WoWModding/Clients \
        --listfile ~/WoWModding/Listfiles/community-listfile.csv \
        --report-dir ./audit-report [--max-per-format N] \
        [--audit-clients 3.3.5a,9.2.7] [--audit-formats wmo,blp]
"""

import collections
import csv
import functools
import json
import os
import shutil
import tempfile

import pytest


def pytest_addoption(parser):
    group = parser.getgroup("audit", "wowlib round-trip audit")
    group.addoption("--clients-dir", action="store", default=None,
                    help="directory containing the client installs (required)")
    group.addoption("--listfile", action="store", default=None,
                    help="community listfile CSV: resolves FileDataIDs for CASC "
                         "clients, and supplies the names Cata-era MPQ archives "
                         "no longer carry internally")
    group.addoption("--report-dir", action="store", default="./audit-report",
                    help="where the CSVs and summary JSON land")
    group.addoption("--max-per-format", action="store", type=int, default=0,
                    help="cap on files per client x format (0 = all)")
    group.addoption("--audit-clients", action="store", default=None,
                    help="comma-separated client dir names to audit (default all present)")
    group.addoption("--audit-formats", action="store", default=None,
                    help="comma-separated formats from wmo,m2,adt,wdt,wdl,blp (default all)")


class AuditCollector:
    """Accumulates per-client x per-format results and writes the report."""

    def __init__(self, report_dir):
        self.report_dir = report_dir
        # {(client, fmt): {"total": n, "ok": n, "failed": n, "skipped": n,
        #                  "stages": Counter, "unknown_chunks": Counter}}
        self.results = {}

    def record(self, client, fmt, rows, unknown_chunks, unknown_examples=None):
        """Store one test's outcome rows and write its CSV.

        rows: list of (path, ok, stage, error, unknown) tuples, where
        `unknown` is the space-separated fourccs that file carried which the
        entity does not model.
        unknown_chunks: Counter of fourcc -> occurrences.
        unknown_examples: fourcc -> a specimen path carrying it, so findings
        are actionable straight from the report.
        """
        stages = collections.Counter(stage for _, ok, stage, _, _ in rows if stage)
        entry = {
            "total": len(rows),
            "ok": sum(1 for _, ok, _, _, _ in rows if ok),
            "failed": sum(1 for _, ok, _, _, _ in rows if not ok),
            "skipped": sum(1 for _, ok, stage, _, _ in rows
                           if ok and stage.startswith("skipped:")),
            "stages": dict(stages),
            "unknown_chunks": {
                fourcc: {"count": count,
                         "example": (unknown_examples or {}).get(fourcc, "")}
                for fourcc, count in unknown_chunks.items()
            },
        }
        self.results[(client, fmt)] = entry

        os.makedirs(self.report_dir, exist_ok=True)
        csv_path = os.path.join(self.report_dir, f"{client}_{fmt}.csv")
        with open(csv_path, "w", newline="", encoding="utf-8") as out:
            writer = csv.writer(out)
            # `unknown` is per FILE, not just the summary's one example per
            # fourcc: when a sweep turns up a chunk wowlib does not model, the
            # question is always "which files carry it?", and re-running a
            # 4-hour audit to find out is the wrong answer. (It is also the only
            # place that record survives — once the chunk IS modelled it stops
            # being unknown and disappears from the tally.)
            writer.writerow(["path", "ok", "stage", "error", "unknown"])
            for path, ok, stage, error, unknown in rows:
                writer.writerow([path, int(ok), stage, error, unknown])
        self._write_summary()
        return entry

    def _write_summary(self):
        """Rewrite summary.json from everything recorded so far (crash-safe)."""
        summary = {}
        for (client, fmt), entry in sorted(self.results.items()):
            summary.setdefault(client, {})[fmt] = entry
        path = os.path.join(self.report_dir, "summary.json")
        with open(path, "w", encoding="utf-8") as out:
            json.dump(summary, out, indent=2)
        self._write_step_summary()

    def _write_step_summary(self):
        """Rewrite the GitHub job Summary page (GITHUB_STEP_SUMMARY) so a
        killed sweep still surfaces everything recorded before it died. This
        step is the file's only writer, so a full rewrite per cell is safe."""
        target = os.environ.get("GITHUB_STEP_SUMMARY")
        if not target:
            return
        lines = ["## Round-trip audit", "",
                 "| client | format | total | ok | failed | skipped | unknown fourccs |",
                 "|---|---|---:|---:|---:|---:|---:|"]
        for (client, fmt), e in sorted(self.results.items()):
            failed = f"**{e['failed']}**" if e["failed"] else "0"
            lines.append(f"| {client} | {fmt} | {e['total']} | {e['ok']} | "
                         f"{failed} | {e['skipped']} | {len(e['unknown_chunks'])} |")
        unknown = [(client, fmt, fourcc, info)
                   for (client, fmt), e in sorted(self.results.items())
                   for fourcc, info in sorted(e["unknown_chunks"].items())]
        if unknown:
            lines += ["", "### Unknown chunks", "",
                      "| client | format | fourcc | count | example |",
                      "|---|---|---|---:|---|"]
            lines += [f"| {client} | {fmt} | {fourcc} | {info['count']} | "
                      f"`{info['example']}` |"
                      for client, fmt, fourcc, info in unknown]
        with open(target, "w", encoding="utf-8") as out:
            out.write("\n".join(lines) + "\n")

    def table(self):
        lines = [f"{'client':<10} {'format':<6} {'total':>8} {'ok':>8} "
                 f"{'failed':>8} {'skipped':>8} {'unknown':>8}"]
        for (client, fmt), e in sorted(self.results.items()):
            lines.append(f"{client:<10} {fmt:<6} {e['total']:>8} {e['ok']:>8} "
                         f"{e['failed']:>8} {e['skipped']:>8} "
                         f"{len(e['unknown_chunks']):>8}")
        return "\n".join(lines)


def pytest_configure(config):
    config._audit_collector = AuditCollector(
        os.path.abspath(config.getoption("--report-dir")))


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    collector = getattr(config, "_audit_collector", None)
    if collector and collector.results:
        terminalreporter.section("wowlib round-trip audit")
        terminalreporter.write_line(collector.table())
        terminalreporter.write_line(f"report dir: {collector.report_dir}")


@pytest.fixture(scope="session")
def audit_options(request):
    clients_dir = request.config.getoption("--clients-dir")
    if not clients_dir:
        pytest.skip("--clients-dir is required to run the audit")
    clients = request.config.getoption("--audit-clients")
    formats = request.config.getoption("--audit-formats")
    return {
        "clients_dir": os.path.abspath(clients_dir),
        "listfile": request.config.getoption("--listfile"),
        "max_per_format": request.config.getoption("--max-per-format"),
        "clients": set(clients.split(",")) if clients else None,
        "formats": set(formats.split(",")) if formats else None,
    }


@pytest.fixture(scope="session")
def collector(request):
    return request.config._audit_collector


@pytest.fixture(scope="session")
def scratch_dir():
    """Session temp dir for the disposable per-client listfile copies
    (FileSystem.open appends registrations to its working listfile, so the
    community CSV is never handed over directly)."""
    with tempfile.TemporaryDirectory(prefix="wowlib-audit-") as tmp:
        yield tmp


@pytest.fixture(scope="session")
def client_contexts(audit_options, scratch_dir):
    """Lazily opened, session-cached per-client state: the FileSystem plus the
    classified enumeration (one open + one enumerate per client, shared by the
    six per-format tests)."""
    import wowlib

    cache = {}

    def get(client_dir_name, version):
        if client_dir_name in cache:
            return cache[client_dir_name]

        root = os.path.join(audit_options["clients_dir"], client_dir_name)
        is_mpq = version.storage_kind == wowlib.StorageKind.Mpq
        locale = _detect_locale(wowlib, root)

        kwargs = {}
        if is_mpq:
            if locale is not None:
                kwargs["locale"] = locale
        else:
            kwargs["locale"] = locale if locale is not None else wowlib.Locale.enUS
            if audit_options["listfile"]:
                working = os.path.join(scratch_dir, f"{client_dir_name}-listfile.csv")
                if not os.path.exists(working):
                    # Copy-then-rename: a copy that dies mid-write (ENOSPC on a
                    # tmpfs /tmp once poisoned a whole client's enumeration with
                    # a truncated listfile) must never look like a finished one.
                    partial = working + ".partial"
                    shutil.copyfile(audit_options["listfile"], partial)
                    os.replace(partial, working)
                kwargs["listfile_csv"] = working

        try:
            fs = wowlib.fs.FileSystem.open(wowlib.fs.FileSystemSettings(
                client_path=root, version=version, **kwargs))
        except Exception as error:  # tool layer broke: that IS a test failure
            pytest.fail(f"{client_dir_name}: FileSystem.open failed: {error}")

        paths = fs.enumerate_paths()  # a raised wowlib.Error fails the test too
        recovered = 0
        if is_mpq and audit_options["listfile"]:
            known = set(paths)
            candidates = _listfile_candidates(audit_options["listfile"]) - known
            paths = list(paths) + [p for p in candidates if fs.exists(p)]
            recovered = len(paths) - len(known)

        cache[client_dir_name] = (fs, _classify(paths))
        # The classification is client-wide; report it here, once, rather than
        # dragging the same diagnostics through every format cell's line.
        _, classified = cache[client_dir_name]
        print(f"\n[{client_dir_name}] enumerated {len(paths)} paths "
              f"({recovered} recovered via the listfile), work "
              f"{ {fmt: len(items) for fmt, items in classified['work'].items()} } "
              f"excluded {dict(classified['diagnostics'])}", flush=True)
        return cache[client_dir_name]

    return get



@functools.lru_cache(maxsize=1)
def _listfile_candidates(listfile_path):
    """Auditable paths the community listfile knows, canonicalized MPQ-style.

    An MPQ archive can only enumerate itself through its own internal
    `(listfile)`, and Cataclysm-era archives stopped shipping one — so
    `enumerate_paths()` legitimately returns almost nothing for 4.3.4 and
    5.4.8 (a full 4.3.4 install yields 125 paths, all from the wow-update
    archives that do carry a listfile), while reads by path work perfectly.
    Trusting enumeration alone therefore audited those clients into a green
    35-file report. The community listfile is the outside name source that
    fills the gap; every candidate is confirmed with exists() before use, so a
    listfile spanning every expansion cannot inject files this client lacks.

    Restricted to the audited extensions: the listfile has well over a million
    rows and probing each against the chain is the expensive half.

    Args:
        listfile_path: the community CSV ('fileDataId;filepath').

    Returns:
        A frozenset of canonical (backslash-separated, lowercase) paths.
    """
    wanted = (".wmo", ".m2", ".adt", ".wdt", ".wdl", ".blp")
    out = set()
    with open(listfile_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            path = line.split(";", 1)[-1].strip().lower()
            if path.endswith(wanted):
                out.add(path.replace("/", "\\"))
    return frozenset(out)


def _detect_locale(wowlib, client_root):
    """Port of the C++ test env's data_dir + find_locale: scan Data/ (or data/)
    for a locale subdirectory, preferring English; None when the install is
    flat (stock vanilla)."""
    data = None
    for name in ("Data", "data"):
        candidate = os.path.join(client_root, name)
        if os.path.isdir(candidate):
            data = candidate
            break
    if data is None:
        return None

    codes = {code for code in dir(wowlib.Locale)
             if len(code) == 4 and not code.startswith("_")}
    present = {entry for entry in os.listdir(data)
               if entry in codes and os.path.isdir(os.path.join(data, entry))}
    for preferred in ("enUS", "enGB", "ruRU"):
        if preferred in present:
            return getattr(wowlib.Locale, preferred)
    if present:
        return getattr(wowlib.Locale, sorted(present)[0])
    return None


_WDT_AUX_SUFFIXES = ("_occ", "_lgt", "_fogs", "_mpv", "_tex", "_wmo", "_psd", "_pd4")
_ADT_SPLIT_SUFFIXES = ("_tex0", "_tex1", "_obj0", "_obj1", "_lod")


def _is_wmo_group(path):
    stem = path[:-4]
    return len(stem) >= 4 and stem[-4] == "_" and stem[-3:].isdigit()


def _classify(paths):
    """Bucket canonical paths by audited format. Mirrors the C++ classifier:
    wmo roots (groups excluded), .m2, bare map wdts, .wdl, .blp, and root
    .adt tiles (the C++ side derives each tile's alpha format itself)."""
    work = {fmt: [] for fmt in ("wmo", "m2", "adt", "wdt", "wdl", "blp")}
    diagnostics = collections.Counter()
    for path in paths:
        if path.endswith(".wmo"):
            if _is_wmo_group(path):
                diagnostics["wmo_groups"] += 1
            else:
                work["wmo"].append(path)
        elif path.endswith(".m2"):
            work["m2"].append(path)
        elif path.endswith((".mdx", ".mdl")):
            diagnostics["mdx_files"] += 1
        elif path.endswith(".wdt"):
            if path[:-4].endswith(_WDT_AUX_SUFFIXES):
                diagnostics["aux_wdt"] += 1
            else:
                work["wdt"].append(path)
        elif path.endswith(".wdl"):
            work["wdl"].append(path)
        elif path.endswith(".blp"):
            work["blp"].append(path)
        elif path.endswith(".adt"):
            if path[:-4].endswith(_ADT_SPLIT_SUFFIXES):
                diagnostics["adt_split_files"] += 1
            else:
                work["adt"].append(path)
    return {"work": work, "diagnostics": diagnostics}
