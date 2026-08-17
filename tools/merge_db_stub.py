"""Assemble the typed client-database stubs into the stubgen-generated tree.

dbdgen emits the fragments (``--py-stubs-dir``): per-era typed table modules
(``era_<era>.pyi``), the tables package init (``tables_init.pyi``) and the
``Table.open`` Literal-overload block (``overloads.pyi``). This tool

- patches ``wowlib/db/__init__.pyi`` in place: makes ``Table`` generic over
  its row type (``Table[R]``), declares ``__iter__``, splices the per-table
  ``open`` overloads in front of the generic fallback, and adds the imports
  they need;
- overwrites ``wowlib/db/tables/__init__.pyi`` and every
  ``wowlib/db/tables/<era>.pyi`` with dbdgen's typed versions (stubgen sees
  the lazy era modules as empty — their classes only exist after first
  attribute access);
- removes a stale flat ``wowlib/db.pyi`` from before the per-era split
  (stubgen never deletes what it no longer emits, and the flat module would
  shadow the package for every type checker).

Idempotent — a marker comment guards the in-place patch, with a pristine
``.pyi.orig`` copy to re-patch over when the fragments change.

    python tools/merge_db_stub.py <stubs>/wowlib <fragments-dir> <eras-csv>
"""

from __future__ import annotations

import sys
from pathlib import Path

MARKER = "# dbdgen-typed-stub: merged"


def patch_db_init(path: Path, overloads_fragment: str) -> bool:
    """Patch ``wowlib/db/__init__.pyi`` in place (see the module doc)."""
    pristine_path = path.with_suffix(".pyi.orig")
    stub = path.read_text(encoding="utf-8")
    if MARKER in stub:
        # Re-patch over the pristine copy (the fragments may have changed).
        if not pristine_path.exists():
            return True
        stub = pristine_path.read_text(encoding="utf-8")
    else:
        pristine_path.write_text(stub, encoding="utf-8")

    # Imports: extend stubgen's own import block; add what it did not need
    # (stubgen always imports `overload` — Table's read/write overload — and
    # emits the `from . import tables as tables` submodule directive itself).
    if "from typing import" in stub:
        stub = stub.replace("from typing import ",
                            "from typing import Generic, Literal, TypeVar, ",
                            1)
    else:
        stub = stub.replace(
            "import wowlib",
            "from typing import Generic, Literal, TypeVar, overload\n\n"
            "import wowlib", 1)
    if "from collections.abc import" in stub:
        stub = stub.replace("from collections.abc import ",
                            "from collections.abc import Iterator, ", 1)
    else:
        stub = stub.replace(
            "from typing import Generic, Literal, TypeVar",
            "from collections.abc import Iterator\n"
            "from typing import Generic, Literal, TypeVar", 1)
    if "import tables as tables" not in stub:
        stub = stub.replace(
            "import wowlib\n",
            "import wowlib\nfrom wowlib.db import tables as tables\n", 1)

    # The row-type parameter (quoted: Record is declared later in the file).
    # PEP 696 default keeps a bare `Table` reading as Table[Record].
    stub = stub.replace(
        "\n\nclass ",
        '\n\nR = TypeVar("R", bound="Record", default="Record")\n\nclass ', 1)

    # Table becomes generic over its row type; the era modules' classes
    # subscript it (`class Map(Table[MapRecord])`).
    generic_ok = "class Table(TableBase):" in stub
    stub = stub.replace("class Table(TableBase):",
                        "class Table(TableBase, Generic[R]):", 1)

    # Row access types through R. The runtime iterates via the sequence
    # protocol (__getitem__/__len__); declare the matching __iter__.
    stub = stub.replace("    def __getitem__(self, index: int) -> Record:",
                        "    def __getitem__(self, index: int) -> R:", 1)
    stub = stub.replace("    def __len__(self) -> int:",
                        "    def __iter__(self) -> Iterator[R]: ...\n"
                        "    def __len__(self) -> int:", 1)

    # Splice the per-table overloads in front of the generic open, which
    # stays as the LAST overload (dynamic table names keep working).
    _, _, overloads = overloads_fragment.partition("Do not edit.\n")
    lines = stub.splitlines(keepends=True)
    out: list[str] = []
    replaced = False
    for i, line in enumerate(lines):
        if (not replaced and line.strip() == "@staticmethod"
                and i + 1 < len(lines)
                and lines[i + 1].lstrip().startswith("def open(")):
            out.append(overloads)
            out.append("    @overload\n")
            # the generic fallback keeps its docstring block untouched
            replaced = True
        out.append(line)
    if not replaced or not generic_ok:
        print("merge_db_stub: could not find "
              f"{'Table.open' if generic_ok else 'class Table'} — "
              "stub layout changed", file=sys.stderr)
        return False

    path.write_text("".join(out) + "\n" + MARKER + "\n", encoding="utf-8")
    print(f"merge_db_stub: {path} patched "
          f"({overloads.count('def open')} table overloads)")
    return True


def main() -> int:
    stubs_root, fragments, eras_csv = (Path(sys.argv[1]), Path(sys.argv[2]),
                                       sys.argv[3])
    eras = [e.strip() for e in eras_csv.split(",") if e.strip()]

    # A stub tree from before the per-era split carries a flat db.pyi module
    # (and possibly the even older db/ rowbase/tables package layout's leaf
    # files, which stubgen now overwrites) that would shadow the db package.
    for stale in ("db.pyi", "db.pyi.orig"):
        (stubs_root / stale).unlink(missing_ok=True)

    if not patch_db_init(stubs_root / "db" / "__init__.pyi",
                         (fragments / "overloads.pyi")
                         .read_text(encoding="utf-8")):
        return 1

    tables_dir = stubs_root / "db" / "tables"
    tables_dir.mkdir(parents=True, exist_ok=True)
    (tables_dir / "__init__.pyi").write_text(
        (fragments / "tables_init.pyi").read_text(encoding="utf-8"),
        encoding="utf-8")
    for era in eras:
        (tables_dir / f"{era}.pyi").write_text(
            (fragments / f"era_{era}.pyi").read_text(encoding="utf-8"),
            encoding="utf-8")
    print(f"merge_db_stub: {len(eras)} typed era modules written to "
          f"{tables_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
