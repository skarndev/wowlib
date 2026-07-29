"""dbdgen — generates wowlib C++ record structs from WoWDBDefs .dbd definitions.

The pipeline: parse (dbd.py) -> match our target client builds to version
blocks and compute effective member lists (emit.py, via targets.py) -> collapse
identical consecutive targets into ranges and emit one header per table plus a
per-era manifest (emit.py).
"""

from dbdgen.dbd import Definition, parse, parse_build
from dbdgen.targets import TARGETS, Target, locstring_langs

__all__ = ["Definition", "parse", "parse_build", "TARGETS", "Target", "locstring_langs"]
