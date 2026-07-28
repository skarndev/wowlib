#!/usr/bin/env python3
"""Exhaustive M2 parse survey over a client install (temp harness).

Walks every ``.m2`` entry of the community listfile, reads each through the
wowlib M2 assembly (baking .skin/.anim/.skel satellites), runs structural
sanity checks on the decoded model, and streams one CSV row per file — the
exhaustive companion to the sampled C++ integration tests, meant to surface
the edge cases a curated sample misses.

Run it from the project root with the bindings venv:

    .venv/bin/python m2_survey.py                    # the full corpus
    .venv/bin/python m2_survey.py --limit 500        # a quick smoke slice
    .venv/bin/python m2_survey.py --sample 5000      # seeded random sample

Statuses in the CSV:
  ok             read + every sanity and round-trip check passed
  roundtrip_fail write -> read -> write was not byte-stable (see checks_failed)
  sanity_fail    read succeeded, structural checks failed (see checks_failed)
  read_error     wowlib raised while reading (error class + message recorded)
  crash          a non-wowlib exception escaped (a wowlib bug — always report)
  encrypted      the file sits behind an unknown TACT key
  absent         the listfile names it but this install does not serve it

Round-trip verification serializes every decoded entity (body, each skin,
the chunked shell, a skel model's SK*1 blocks), reads the bytes back into a
fresh instance and re-serializes: canonical writes are deterministic, so the
two byte strings must match exactly — a bitwise oracle that NaN floats in
client data cannot spoof (unlike object equality). Disable with
--no-roundtrip for a faster read-only sweep.

The install's own era matters: a 9.2.7 install reports every
post-Shadowlands listfile entry as absent — that is data, not noise.
Ctrl-C keeps the partial CSV and prints the summary for what ran.
"""

from __future__ import annotations

import argparse
import collections
import csv
import os
import random
import shutil
import sys
import time

import wowlib
from wowlib.formats import m2 as m2_mod

# The script is deliberately version-generic; the stubs type shell/skel per
# era, so the decoded model is handled as Any past for_version.
from typing import Any

try:
    import numpy as np
except ImportError:  # the max()-fallbacks below stay correct, just slower
    np = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clients-dir",
                        default=os.environ.get("WOWLIB_TEST_CLIENTS_DIR",
                                               os.path.expanduser("~/WoWModding/Clients")),
                        help="directory holding the client installs")
    parser.add_argument("--client", default="WoWCircle 9.2.7",
                        help="client install directory name")
    parser.add_argument("--version", default="shadowlands",
                        help="wowlib.versions constant to open/parse as")
    parser.add_argument("--listfile",
                        default=os.environ.get(
                            "WOWLIB_TEST_LISTFILE",
                            os.path.expanduser("~/WoWModding/Listfiles/community-listfile.csv")),
                        help="community listfile CSV (fdid;path)")
    parser.add_argument("--out", default="m2_survey.csv", help="output CSV path")
    parser.add_argument("--limit", type=int, default=0,
                        help="stop after N entries (0 = all)")
    parser.add_argument("--sample", type=int, default=0,
                        help="survey a random sample of N entries (seeded, 0 = off)")
    parser.add_argument("--seed", type=int, default=20260724, help="--sample shuffle seed")
    parser.add_argument("--no-bar", action="store_true",
                        help="plain periodic lines instead of the live bar (for logs)")
    parser.add_argument("--no-roundtrip", action="store_true",
                        help="skip the write->read->write byte-stability checks")
    return parser.parse_args()


class Progress:
    """A dependency-free single-line progress bar: bar, rate, ETA and the
    live status tally. Falls back to periodic plain lines when stdout is not
    a TTY (or --no-bar), so `nohup`/tee logs stay readable."""

    STATUS_ORDER = ["ok", "roundtrip_fail", "sanity_fail", "read_error", "crash",
                    "encrypted", "absent"]
    SHORT = {"ok": "ok", "roundtrip_fail": "rt", "sanity_fail": "sane",
             "read_error": "err", "crash": "CRASH", "encrypted": "enc", "absent": "abs"}

    def __init__(self, total: int, live: bool):
        self.total = total
        self.live = live and sys.stdout.isatty()
        self.started = time.monotonic()
        self.last_paint = 0.0

    @staticmethod
    def _hms(seconds: float) -> str:
        seconds = int(seconds)
        if seconds >= 3600:
            return f"{seconds // 3600}:{seconds % 3600 // 60:02}:{seconds % 60:02}"
        return f"{seconds // 60}:{seconds % 60:02}"

    def paint(self, done: int, counts: collections.Counter, force: bool = False) -> None:
        now = time.monotonic()
        if not force and now - self.last_paint < (0.1 if self.live else 30.0):
            return
        self.last_paint = now

        elapsed = now - self.started
        rate = done / elapsed if elapsed > 0 else 0.0
        remaining = (self.total - done) / rate if rate > 0 else 0.0
        tally = "  ".join(f"{self.SHORT[s]} {counts[s]}"
                          for s in self.STATUS_ORDER if counts[s])

        if not self.live:
            print(f"{done}/{self.total} ({100.0 * done / max(self.total, 1):.1f}%) "
                  f"| {rate:.0f}/s | eta {self._hms(remaining)} | {tally}", flush=True)
            return

        width = shutil.get_terminal_size().columns
        head = f" {done}/{self.total} {100.0 * done / max(self.total, 1):5.1f}%"
        tail = f" {rate:4.0f}/s  eta {self._hms(remaining)}  {tally}"
        bar_room = max(10, width - len(head) - len(tail) - 3)
        filled = int(bar_room * done / max(self.total, 1))
        bar = "█" * filled + "░" * (bar_room - filled)
        line = f"{head} {bar}{tail}"
        print(f"\r\x1b[2K{line[:width - 1]}", end="", flush=True)

    def finish(self, done: int, counts: collections.Counter) -> None:
        self.paint(done, counts, force=True)
        if self.live:
            print()


def load_m2_entries(listfile: str) -> list[tuple[int, str]]:
    """Every (fdid, path) whose path ends in .m2, listfile order."""
    entries: list[tuple[int, str]] = []
    with open(listfile, encoding="utf-8", errors="replace", newline="") as handle:
        for line in handle:
            line = line.rstrip("\r\n")
            sep = line.find(";")
            if sep < 0:
                continue
            path = line[sep + 1:]
            if path.endswith(".m2"):
                try:
                    entries.append((int(line[:sep]), path))
                except ValueError:
                    continue
    return entries


def vec_max(vec) -> int:
    """Max of an opaque scalar vector: zero-copy numpy when available."""
    if len(vec) == 0:
        return -1
    if np is not None:
        try:
            return int(np.asarray(vec).max())
        except (TypeError, ValueError):
            pass
    return max(vec)


def check_skin(skin, n_global_vertices: int, n_materials: int, failed: list[str],
               tag: str) -> None:
    """Structural checks on one LOD view against the body it belongs to."""
    profile = skin.profile
    n_local = len(profile.vertices)
    if len(profile.indices) % 3 != 0:
        failed.append(f"{tag}:indices_not_triangles")
    if n_local and n_global_vertices and vec_max(profile.vertices) >= n_global_vertices:
        failed.append(f"{tag}:vertex_lookup_out_of_range")
    if len(profile.indices) and n_local and vec_max(profile.indices) >= n_local:
        failed.append(f"{tag}:index_out_of_range")
    for section in profile.submeshes:
        # level extends ONLY the 16-bit triangle start (index lists exceed
        # 64k long before the local vertex list does — verified on
        # bloodtrollfemale_caster, where level=1 sections' vertex ranges are
        # plain 16-bit values)
        index_start = section.index_start + (section.level << 16)
        if section.vertex_start + section.vertex_count > n_local:
            failed.append(f"{tag}:submesh_vertices_out_of_range")
            break
        if index_start + section.index_count > len(profile.indices):
            failed.append(f"{tag}:submesh_indices_out_of_range")
            break
    for batch in profile.batches:
        if batch.material_index >= n_materials:
            failed.append(f"{tag}:batch_material_out_of_range")
            break
        if batch.skin_section_index >= len(profile.submeshes):
            failed.append(f"{tag}:batch_submesh_out_of_range")
            break


def sanity_checks(model) -> list[str]:
    """Cheap structural invariants on a decoded model; returns failed names."""
    failed: list[str] = []
    data = model.data

    if data.num_skin_profiles != len(model.skins):
        failed.append("skin_count_mismatch")

    # skel-based models keep bones/sequences on the skeleton
    skel_based = len(model.shell.skeleton_fdid) > 0
    bones = model.skel.bone_block.bones if skel_based else data.bones
    sequences = model.skel.sequence_block.sequences if skel_based else data.sequences

    n_bones = len(bones)
    for bone in bones:
        if bone.parent_bone != -1 and not 0 <= bone.parent_bone < n_bones:
            failed.append("bone_parent_out_of_range")
            break

    n_sequences = len(sequences)
    for seq in sequences:
        if seq.flags & 0x40 and seq.alias_next >= n_sequences:
            failed.append("sequence_alias_out_of_range")
            break

    # per-sequence tracks: timestamp/value outer arrays must agree, and never
    # exceed the sequence count (global-sequence tracks legitimately hold 1)
    for bone in bones:
        for track in (bone.translation, bone.rotation, bone.scale):
            if len(track.timestamps) != len(track.values):
                failed.append("bone_track_outer_mismatch")
                break
            if track.global_sequence == 0xFFFF and len(track.timestamps) > max(n_sequences, 1):
                failed.append("bone_track_more_outers_than_sequences")
                break
        else:
            continue
        break

    n_textures = len(data.textures)
    if len(data.texture_lookup_table) and n_textures \
            and vec_max(data.texture_lookup_table) >= n_textures:
        failed.append("texture_lookup_out_of_range")

    n_vertices = len(data.vertices)
    n_materials = len(data.materials)
    for i, skin in enumerate(model.skins):
        check_skin(skin, n_vertices, n_materials, failed, f"skin{i}")
    for i, skin in enumerate(model.lod_skins):
        check_skin(skin, n_vertices, n_materials, failed, f"lod{i}")

    return failed


def roundtrip_checks(model) -> list[str]:
    """Byte-stability of every decoded entity: write it, read the bytes into
    a fresh instance, write again — the two serializations must match
    exactly. External-sequence data serializes inline without a sink
    context, so each cycle is self-contained. Returns failed check names."""
    failed: list[str] = []

    def cycle(entity, tag: str) -> None:
        blob = entity.write()
        fresh = type(entity)()
        try:
            fresh.read(blob)
        except wowlib.Error as error:
            failed.append(f"{tag}:reread_{type(error).__name__}")
            return
        if fresh.write() != blob:
            failed.append(f"{tag}:not_byte_stable")

    cycle(model.data, "body")
    for i, skin in enumerate(model.skins):
        cycle(skin, f"skin{i}")
    for i, skin in enumerate(model.lod_skins):
        cycle(skin, f"lod{i}")
    if not model.shell.md21.empty:
        cycle(model.shell, "shell")
    if len(model.shell.skeleton_fdid) > 0:
        cycle(model.skel.sequence_block, "sks1")
        cycle(model.skel.bone_block, "skb1")
        cycle(model.skel.attachment_block, "ska1")
    return failed


def survey(args: argparse.Namespace) -> int:
    settings = wowlib.fs.FileSystemSettings(
        client_path=os.path.join(args.clients_dir, args.client),
        version=getattr(wowlib.versions, args.version),
        listfile_csv=args.listfile,
    )
    expansion = wowlib.to_expansion(getattr(wowlib.versions, args.version))
    assert expansion is not None

    entries = load_m2_entries(args.listfile)
    print(f"{len(entries)} .m2 entries in the listfile")
    if args.sample:
        random.Random(args.seed).shuffle(entries)
        entries = entries[: args.sample]
    if args.limit:
        entries = entries[: args.limit]
    print(f"surveying {len(entries)} models -> {args.out}")

    counts: collections.Counter[str] = collections.Counter()
    error_kinds: collections.Counter[str] = collections.Counter()
    check_kinds: collections.Counter[str] = collections.Counter()
    progress = Progress(len(entries), live=not args.no_bar)
    done = 0

    columns = ["fdid", "path", "status", "error", "detail", "chunked", "skel",
               "n_vertices", "n_bones", "n_sequences", "n_skins", "n_lod_skins",
               "n_particles", "checks_failed"]
    # not a with-statement: the context manager's __enter__ is untyped in the
    # stubs today, and fs needs its precise type below
    fs = wowlib.fs.FileSystem.open(settings)
    with open(args.out, "w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=columns)
        writer.writeheader()

        try:
            for fdid, path in entries:
                row = {"fdid": fdid, "path": path}
                key = wowlib.FileKey(path, wowlib.FileDataID(fdid))
                try:
                    if not fs.exists(key):
                        row["status"] = "absent"
                        counts["absent"] += 1
                        writer.writerow(row)
                        done += 1
                        progress.paint(done, counts)
                        continue

                    model: Any = m2_mod.M2.for_version(expansion)
                    model.read(fs, key)

                    skel_based = len(model.shell.skeleton_fdid) > 0
                    bones = (model.skel.bone_block.bones if skel_based
                             else model.data.bones)
                    sequences = (model.skel.sequence_block.sequences if skel_based
                                 else model.data.sequences)
                    row.update(chunked=int(not model.shell.md21.empty),
                               skel=int(skel_based),
                               n_vertices=len(model.data.vertices),
                               n_bones=len(bones),
                               n_sequences=len(sequences),
                               n_skins=len(model.skins),
                               n_lod_skins=len(model.lod_skins),
                               n_particles=len(model.data.particle_emitters))

                    failed = sanity_checks(model)
                    roundtrip_failed = ([] if args.no_roundtrip
                                        else roundtrip_checks(model))
                    if failed or roundtrip_failed:
                        row["status"] = ("roundtrip_fail" if roundtrip_failed
                                         else "sanity_fail")
                        row["checks_failed"] = " ".join(failed + roundtrip_failed)
                        counts[row["status"]] += 1
                        for name in failed + roundtrip_failed:
                            check_kinds[name.split(":")[-1]] += 1
                    else:
                        row["status"] = "ok"
                        counts["ok"] += 1
                except wowlib.Error as error:
                    kind = type(error).__name__
                    if kind == "EncryptedContent":
                        row["status"] = "encrypted"
                        counts["encrypted"] += 1
                    else:
                        row["status"] = "read_error"
                        counts["read_error"] += 1
                    row["error"] = kind
                    row["detail"] = str(error)
                    error_kinds[kind] += 1
                except Exception as error:  # noqa: BLE001 — a crash IS the finding
                    row["status"] = "crash"
                    row["error"] = type(error).__name__
                    row["detail"] = str(error)
                    counts["crash"] += 1
                    error_kinds[type(error).__name__] += 1
                writer.writerow(row)
                done += 1
                if done % 200 == 0:
                    out.flush()
                progress.paint(done, counts)
        except KeyboardInterrupt:
            progress.finish(done, counts)
            print(f"interrupted at {done}/{len(entries)}; partial CSV kept")
        else:
            progress.finish(done, counts)
    fs.close()

    elapsed = time.monotonic() - progress.started
    print(f"\ndone in {Progress._hms(elapsed)} — {done} surveyed, CSV: {args.out}")
    for status, count in counts.most_common():
        print(f"  {status:12} {count}")
    if error_kinds:
        print("error kinds:")
        for kind, count in error_kinds.most_common(15):
            print(f"  {kind:28} {count}")
    if check_kinds:
        print("failed checks:")
        for kind, count in check_kinds.most_common(15):
            print(f"  {kind:36} {count}")

    served = done - counts["absent"]
    troubled = (counts["read_error"] + counts["crash"] + counts["sanity_fail"]
                + counts["roundtrip_fail"])
    if served:
        print(f"parse health: {served - troubled}/{served} clean "
              f"({100.0 * (served - troubled) / served:.2f}%)")
    return 1 if counts["crash"] else 0


if __name__ == "__main__":
    sys.exit(survey(parse_args()))
