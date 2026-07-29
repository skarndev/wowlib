"""CLI driver: walk a WoWDBDefs definitions directory and emit the generated
table headers + per-era manifests.

    python -m dbdgen --definitions <WoWDBDefs>/definitions \
                     --out <build>/generated/db \
                     --eras vanilla,tbc,wotlk [--tables Map,AreaTable]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from dbdgen import dbd
from dbdgen.emit import (build_members, collapse, emit_manifest, emit_table, snake,
                         write_if_changed)
from dbdgen.targets import TARGETS_BY_ERA


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="dbdgen")
    parser.add_argument("--definitions", type=Path, required=True,
                        help="the WoWDBDefs definitions/ directory")
    parser.add_argument("--out", type=Path, required=True,
                        help="output root; headers land under <out>/wowlib/db/tables/")
    parser.add_argument("--eras", required=True,
                        help="comma-separated wowlib era names (vanilla,tbc,wotlk,...)")
    parser.add_argument("--tables", default=None,
                        help="comma-separated table subset (default: all)")
    args = parser.parse_args(argv)

    eras = [era.strip() for era in args.eras.split(",") if era.strip()]
    unknown = [era for era in eras if era not in TARGETS_BY_ERA]
    if unknown:
        parser.error(f"unknown eras: {', '.join(unknown)}")
    targets = sorted((TARGETS_BY_ERA[era] for era in eras), key=lambda t: t.version)
    only = set(args.tables.split(",")) if args.tables else None

    tables_dir = args.out / "wowlib" / "db" / "tables"
    manifest: dict[str, list[str]] = {era: [] for era in eras}
    emitted = 0
    skipped_no_block = 0
    warnings: list[str] = []

    for path in sorted(args.definitions.glob("*.dbd")):
        table = path.stem
        if only is not None and table not in only:
            continue
        try:
            defn = dbd.parse(path.read_text(encoding="utf-8-sig"))
        except dbd.DbdError as error:
            warnings.append(f"{table}: {error}")
            continue

        per_target = []
        for target in targets:
            block = defn.block_for_build(target.version)
            if block is None:
                continue
            try:
                per_target.append((target, build_members(defn, block, target)))
            except dbd.DbdError as error:
                warnings.append(f"{table} ({target.era}): {error}")
        if not per_target:
            skipped_no_block += 1
            continue

        ranges = collapse(per_target)
        write_if_changed(tables_dir / f"{snake(table)}.hpp", emit_table(table, ranges))
        emitted += 1
        for target, _ in per_target:
            manifest[target.era].append(table)

    for era in eras:
        write_if_changed(tables_dir / f"manifest_{era}.hpp",
                         emit_manifest(era, manifest[era]))

    print(f"dbdgen: {emitted} tables emitted "
          f"({', '.join(f'{era}: {len(manifest[era])}' for era in eras)}); "
          f"{skipped_no_block} without a matching version block; "
          f"{len(warnings)} warnings")
    for warning in warnings:
        print(f"dbdgen: warning: {warning}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
