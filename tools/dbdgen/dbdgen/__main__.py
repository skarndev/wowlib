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
from dbdgen.emit import (Range, build_members, collapse, emit_all_tables,
                         emit_cs_facades, emit_manifest, emit_py_era_stub,
                         emit_py_open_overloads, emit_py_tables_init,
                         emit_schema_blob, emit_table, snake,
                         write_bytes_if_changed, write_if_changed)
from dbdgen.targets import TARGETS_BY_ERA

# Generated C++/Python identifiers come straight from the table name.
# WoWDBDefs contains exactly one name that is not a valid identifier:
# "Item-sparse", the Cataclysm..WoD ancestor of ItemSparse (a separate .dbd
# with its own columns; their build ranges never overlap). Its generated
# family is renamed here; the record's table_name string keeps the on-disk
# truth ("Item-sparse", as in DBFilesClient/Item-sparse.db2). Any future
# non-identifier name must be added here — dbdgen refuses it otherwise.
IDENT_RENAMES = {"Item-sparse": "ItemSparseLegacy"}


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
    parser.add_argument(
        "--cs-facades-out", type=Path, default=None,
        help="emit the typed pure-C# facade classes (Facades.<i>.cs) into "
             "this directory — plain C# over the generic Table, packed into "
             "the NuGet beside the generated wrapper")
    parser.add_argument(
        "--py-stubs-dir", type=Path, default=None,
        help="emit the typed-completion stub fragments (per-era table "
             "modules + Table.open overloads) into this directory — "
             "assembled into the stubgen tree by tools/merge_db_stub.py")
    parser.add_argument(
        "--schema-blob-out", type=Path, default=None,
        help="emit the compact binary schema blob (WDBS) to this file — the "
             "runtime/consteval schema source for the generic DB engine")
    args = parser.parse_args(argv)

    eras = [era.strip() for era in args.eras.split(",") if era.strip()]
    unknown = [era for era in eras if era not in TARGETS_BY_ERA]
    if unknown:
        parser.error(f"unknown eras: {', '.join(unknown)}")
    targets = sorted((TARGETS_BY_ERA[era] for era in eras), key=lambda t: t.version)
    only = set(args.tables.split(",")) if args.tables else None

    tables_dir = args.out / "wowlib" / "db" / "tables"
    manifest: dict[str, list[str]] = {era: [] for era in eras}
    table_ranges: list[tuple[str, list[Range]]] = []
    disk_names: dict[str, str] = {}
    emitted = 0
    skipped_no_block = 0
    warnings: list[str] = []

    for path in sorted(args.definitions.glob("*.dbd")):
        dbd_name = path.stem
        table = IDENT_RENAMES.get(dbd_name, dbd_name)
        if not table.isidentifier():
            warnings.append(f"{dbd_name}: not a valid identifier — add an "
                            f"IDENT_RENAMES entry for it")
            continue
        if only is not None and table not in only and dbd_name not in only:
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
        write_if_changed(tables_dir / f"{snake(table)}.hpp",
                         emit_table(table, ranges, dbd_name=dbd_name))
        emitted += 1
        table_ranges.append((table, ranges))
        disk_names[table] = dbd_name
        for target, _ in per_target:
            manifest[target.era].append(table)

    for era in eras:
        write_if_changed(tables_dir / f"manifest_{era}.hpp",
                         emit_manifest(era, manifest[era]))

    # The whole-surface umbrella (sorted, so the file is stable across runs).
    write_if_changed(tables_dir / "all.hpp",
                     emit_all_tables(sorted(t for t, _ in table_ranges)))

    if args.cs_facades_out is not None:
        parts = emit_cs_facades(table_ranges, disk_names)
        args.cs_facades_out.mkdir(parents=True, exist_ok=True)
        for i, part in enumerate(parts):
            write_if_changed(args.cs_facades_out / f"Facades.{i}.cs", part)
        print(f"dbdgen: {len(parts)} C# facade files emitted to "
              f"{args.cs_facades_out}")

    if args.py_stubs_dir is not None:
        args.py_stubs_dir.mkdir(parents=True, exist_ok=True)
        write_if_changed(args.py_stubs_dir / "overloads.pyi",
                         emit_py_open_overloads(table_ranges, targets))
        write_if_changed(args.py_stubs_dir / "tables_init.pyi",
                         emit_py_tables_init(targets))
        for target in targets:
            write_if_changed(args.py_stubs_dir / f"era_{target.era}.pyi",
                             emit_py_era_stub(target, table_ranges))
        print(f"dbdgen: typed stub fragments ({len(targets)} era modules) "
              f"emitted to {args.py_stubs_dir}")

    if args.schema_blob_out is not None:
        blob = emit_schema_blob(table_ranges, disk_names)
        write_bytes_if_changed(args.schema_blob_out, blob)
        print(f"dbdgen: schema blob ({len(blob)} bytes, "
              f"{len(table_ranges)} tables) emitted to {args.schema_blob_out}")

    print(f"dbdgen: {emitted} tables emitted "
          f"({', '.join(f'{era}: {len(manifest[era])}' for era in eras)}); "
          f"{skipped_no_block} without a matching version block; "
          f"{len(warnings)} warnings")
    for warning in warnings:
        print(f"dbdgen: warning: {warning}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
