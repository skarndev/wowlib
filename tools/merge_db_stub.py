"""Merge dbdgen's typed-completion fragment into the stubgen-generated
wowlib/db.pyi (in place): replace the single generic ``Table.open`` with the
per-table ``Literal`` overload set (generic fallback last), append the typed
``<X>Record``/``<X>Table`` classes, and add the typing imports. Idempotent —
a marker comment guards against double application.

    python tools/merge_db_stub.py <stubs>/wowlib/db.pyi <fragment.pyi>
"""

from __future__ import annotations

import sys
from pathlib import Path

MARKER = "# dbdgen-typed-stub: merged"


def main() -> int:
    stub_path, fragment_path = Path(sys.argv[1]), Path(sys.argv[2])
    pristine_path = stub_path.with_suffix(".pyi.orig")
    stub = stub_path.read_text(encoding="utf-8")
    if MARKER in stub:
        # Re-merge over the pristine copy (the fragment may have changed).
        if not pristine_path.exists():
            return 0
        stub = pristine_path.read_text(encoding="utf-8")
    else:
        pristine_path.write_text(stub, encoding="utf-8")
    fragment = fragment_path.read_text(encoding="utf-8")
    head, _, rest = fragment.partition("# --- overloads ---\n")
    overloads, _, classes = rest.partition("# --- classes ---\n")

    # Imports: stubgen emits an import block at the top; extend it.
    if "from typing import" in stub:
        stub = stub.replace("from typing import ",
                            "from typing import Literal, overload, ", 1)
    else:
        stub = stub.replace("import wowlib",
                            "from typing import Literal, overload\n\n"
                            "import wowlib", 1)
    if "from collections.abc import" in stub:
        stub = stub.replace("from collections.abc import ",
                            "from collections.abc import Iterator, ", 1)
    else:
        stub = stub.replace("from typing import Literal, overload",
                            "from collections.abc import Iterator\n"
                            "from typing import Literal, overload", 1)

    # Iteration types: the runtime iterates through the sequence protocol
    # (__getitem__/__len__); declare the matching __iter__ so checkers agree.
    stub = stub.replace("    def __len__(self) -> int:",
                        "    def __iter__(self) -> Iterator[Record]: ...\n"
                        "    def __len__(self) -> int:", 1)

    # Replace the generic open (keeping it as the LAST overload). stubgen
    # emits exactly one `@staticmethod\n    def open(...` inside class Table.
    lines = stub.splitlines(keepends=True)
    out: list[str] = []
    i = 0
    replaced = False
    while i < len(lines):
        line = lines[i]
        if (not replaced and line.strip() == "@staticmethod"
                and i + 1 < len(lines)
                and lines[i + 1].lstrip().startswith("def open(")):
            out.append(overloads)
            out.append("    @overload\n")
            # the generic fallback keeps its docstring block untouched
            replaced = True
        out.append(line)
        i += 1
    if not replaced:
        print("merge_db_stub: could not find Table.open — stub layout changed",
              file=sys.stderr)
        return 1

    merged = "".join(out) + "\n" + MARKER + "\n\n" + classes
    stub_path.write_text(merged, encoding="utf-8")
    print(f"merge_db_stub: {stub_path} merged "
          f"({overloads.count('def open')} table overloads)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
