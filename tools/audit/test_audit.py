"""The exhaustive round-trip audit sweep: every client x every format.

Each test opens one client install (session-cached), takes the classified
enumeration for its format, and round-trips every file through
``wowlib.audit.Auditor.roundtrip`` — the C++ side dispatches versions,
byte-compares the byte-perfect formats and write-stability-compares the offset
ones. Outcomes land in the report (CSV per client x format + summary JSON);
nothing about a FILE's round-trip is asserted — failures are the audit's data.
"""

import collections
import os
import time

import pytest

# Every release wowlib targets, by canonical install-directory name — the same
# table as tests/integration/test_all_clients_open.cpp, with the descriptive
# pre-CI local install names accepted as fallbacks (integration_env.hpp does
# the same). Each row: (canonical id, candidate directory names, version).
CLIENTS = [
    ("1.12.1", ("1.12.1",), "vanilla"),
    ("1.12.2", ("1.12.2", "WoW Classic 1.12.2"), "vanilla"),
    ("2.4.3", ("2.4.3", "WoW TBC 2.4.3"), "tbc"),
    ("3.3.5a", ("3.3.5a", "World of Warcraft 3.3.5a"), "wotlk"),
    ("4.3.4", ("4.3.4", "WoW Cata 4.3.4"), "cata"),
    ("5.4.8", ("5.4.8",), "mop"),
    ("6.2.3", ("6.2.3",), "wod"),
    ("6.2.4", ("6.2.4",), "wod"),
    ("7.3.5", ("7.3.5",), "legion"),
    ("8.3.7", ("8.3.7",), "bfa"),
    ("9.2.7", ("9.2.7", "WoWCircle 9.2.7"), "shadowlands"),
    ("10.2.7", ("10.2.7",), "dragonflight"),
    ("11.2.7", ("11.2.7",), "tww"),
]

FORMATS = ["wmo", "m2", "adt", "wdt", "wdl", "blp"]


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize(("client_id", "candidates", "version_name"), CLIENTS,
                         ids=[client_id for client_id, _, _ in CLIENTS])
def test_roundtrip(client_id, candidates, version_name, fmt, audit_options,
                   client_contexts, collector):
    import wowlib

    if audit_options["clients"] is not None and client_id not in audit_options["clients"]:
        pytest.skip(f"{client_id} not selected via --audit-clients")
    if audit_options["formats"] is not None and fmt not in audit_options["formats"]:
        pytest.skip(f"{fmt} not selected via --audit-formats")
    client_dir = next(
        (name for name in candidates
         if os.path.isdir(os.path.join(audit_options["clients_dir"], name))), None)
    if client_dir is None:
        pytest.skip(f"no client install named '{client_id}' (or a known alias) "
                    "under the clients dir")

    version = getattr(wowlib.versions, version_name)
    fs, classified = client_contexts(client_dir, version)

    work = classified["work"][fmt]
    cap = audit_options["max_per_format"]
    if cap:
        work = work[:cap]

    # Streamed progress: a cell can grind for hours, and pytest's capture
    # would otherwise leave the CI log silent the whole time (the workflow
    # runs with -s for exactly this reason). On GitHub Actions each cell
    # additionally becomes a collapsible ::group:: in the log view.
    on_actions = os.environ.get("GITHUB_ACTIONS") == "true"
    if on_actions:
        # The leading newline detaches the marker from pytest's ./s progress
        # chars (-s interleaves them mid-line otherwise).
        print(f"\n::group::{client_id} {fmt} — {len(work)} files", flush=True)

    rows = []
    unknown_chunks = collections.Counter()
    unknown_examples = {}
    ok_n = failed_n = skipped_n = 0
    started = last_emit = time.monotonic()
    try:
        for i, path in enumerate(work, 1):
            try:
                report = wowlib.audit.Auditor.roundtrip(fs, path, version)
            except Exception as error:  # the C++ side guards; a raise is tool-layer data
                rows.append((path, False, "exception", repr(error)))
            else:
                rows.append((path, report.ok, report.stage, report.error))
                for fourcc in report.unknown_chunks:
                    unknown_chunks[fourcc] += 1
                    unknown_examples.setdefault(fourcc, path)
            _, ok, stage, _ = rows[-1]
            if not ok:
                failed_n += 1
            elif stage.startswith("skipped"):
                skipped_n += 1
            else:
                ok_n += 1
            now = time.monotonic()
            if now - last_emit >= 30:
                rate = i / (now - started)
                print(f"[{client_id}/{fmt}] {i}/{len(work)} ok={ok_n} "
                      f"failed={failed_n} skipped={skipped_n} ({rate:.1f} files/s)",
                      flush=True)
                last_emit = now

        entry = collector.record(client_id, fmt, rows, unknown_chunks, unknown_examples)
        print(f"[{client_id}] {fmt}: total={entry['total']} ok={entry['ok']} "
              f"failed={entry['failed']} skipped={entry['skipped']} "
              f"unknown_fourccs={len(entry['unknown_chunks'])}", flush=True)
    finally:
        if on_actions:
            print("::endgroup::", flush=True)
