"""Parser for WoWDBDefs .dbd files (https://github.com/wowdev/WoWDBDefs).

A .dbd file is a COLUMNS paragraph followed by version-block paragraphs,
separated by blank lines:

    COLUMNS
    int ID
    int<Map::ID> ParentMapID     // a foreign key
    locstring MapName_lang
    float Unverified?            // '?' marks an unverified column name

    LAYOUT 0E84A21C, DEADBEEF
    BUILD 1.12.1.5875
    BUILD 1.11.0.5344-1.12.0.5595
    COMMENT free text
    $id$ID<32>
    Directory
    Flags<u32>[2]

Version-block entries carry the physical shape (<u32> integer sizing, [N]
arrays, $id$/$relation$/$noninline$ roles); unsized entries take their type
from the COLUMNS declaration (float / string / locstring).
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

Build = tuple[int, int, int, int]

_COLUMN_RE = re.compile(
    r"^(?P<type>int|float|string|locstring)"
    r"(?:<(?P<foreign>[^>]+)>)?"
    r"\s+(?P<name>[A-Za-z0-9_]+)(?P<unverified>\?)?$"
)
_ENTRY_RE = re.compile(
    r"^(?:\$(?P<annotations>[a-z,]+)\$)?"
    r"(?P<name>[A-Za-z0-9_]+)"
    r"(?:<(?P<unsigned>u)?(?P<bits>\d+)>)?"
    r"(?:\[(?P<array>\d+)\])?$"
)
_BUILD_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)$")


class DbdError(ValueError):
    """A .dbd file (or one line of it) does not parse."""


@dataclass
class ColumnDecl:
    """One COLUMNS declaration: the logical type and identity of a column."""

    type: str  # int / float / string / locstring
    name: str
    foreign: str | None = None
    verified: bool = True
    comment: str | None = None


@dataclass
class Entry:
    """One version-block column entry: the physical shape in record order."""

    name: str
    annotations: tuple[str, ...] = ()
    bits: int | None = None
    unsigned: bool = False
    array_len: int | None = None
    comment: str | None = None

    @property
    def is_id(self) -> bool:
        return "id" in self.annotations

    @property
    def is_relation(self) -> bool:
        return "relation" in self.annotations

    @property
    def noninline(self) -> bool:
        return "noninline" in self.annotations


@dataclass
class VersionBlock:
    """One version paragraph: the builds/layouts it covers and its entries."""

    layouts: list[str] = field(default_factory=list)
    builds: list[tuple[Build, Build]] = field(default_factory=list)  # inclusive ranges
    comment: str | None = None
    entries: list[Entry] = field(default_factory=list)

    def matches_build(self, build: Build) -> bool:
        return any(lo <= build <= hi for lo, hi in self.builds)


@dataclass
class Definition:
    """A parsed .dbd file."""

    columns: dict[str, ColumnDecl]
    blocks: list[VersionBlock]

    def block_for_build(self, build: Build) -> VersionBlock | None:
        """The first version block covering ``build``, or None."""
        for block in self.blocks:
            if block.matches_build(build):
                return block
        return None


def parse_build(text: str) -> Build:
    """Parse a ``major.minor.patch.build`` spelling."""
    m = _BUILD_RE.match(text.strip())
    if not m:
        raise DbdError(f"malformed build number: {text!r}")
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)))


def _split_comment(line: str) -> tuple[str, str | None]:
    body, sep, comment = line.partition("//")
    return body.strip(), (comment.strip() if sep else None)


def _parse_columns(lines: list[str], columns: dict[str, ColumnDecl]) -> None:
    for line in lines:
        body, comment = _split_comment(line)
        if not body:
            continue
        m = _COLUMN_RE.match(body)
        if not m:
            raise DbdError(f"malformed COLUMNS line: {line!r}")
        columns[m.group("name")] = ColumnDecl(
            type=m.group("type"),
            name=m.group("name"),
            foreign=m.group("foreign"),
            verified=m.group("unverified") is None,
            comment=comment,
        )


def _parse_block(lines: list[str]) -> VersionBlock:
    block = VersionBlock()
    for line in lines:
        body, comment = _split_comment(line)
        if not body:
            continue
        if body.startswith("LAYOUT "):
            block.layouts.extend(part.strip() for part in body[len("LAYOUT "):].split(","))
        elif body.startswith("BUILD "):
            for part in body[len("BUILD "):].split(","):
                part = part.strip()
                if "-" in part:
                    lo, hi = part.split("-", 1)
                    block.builds.append((parse_build(lo), parse_build(hi)))
                else:
                    build = parse_build(part)
                    block.builds.append((build, build))
        elif body.startswith("COMMENT"):
            block.comment = body[len("COMMENT"):].strip() or comment
        else:
            m = _ENTRY_RE.match(body)
            if not m:
                raise DbdError(f"malformed version-block entry: {line!r}")
            annotations = tuple(
                a.strip() for a in (m.group("annotations") or "").split(",") if a.strip()
            )
            block.entries.append(
                Entry(
                    name=m.group("name"),
                    annotations=annotations,
                    bits=int(m.group("bits")) if m.group("bits") else None,
                    unsigned=m.group("unsigned") is not None,
                    array_len=int(m.group("array")) if m.group("array") else None,
                    comment=comment,
                )
            )
    if not block.builds and not block.layouts:
        raise DbdError("version block carries neither BUILD nor LAYOUT")
    return block


def parse(text: str) -> Definition:
    """Parse a whole .dbd file."""
    paragraphs: list[list[str]] = []
    current: list[str] = []
    for raw in text.lstrip("﻿").splitlines():
        line = raw.strip()
        if not line:
            if current:
                paragraphs.append(current)
                current = []
        else:
            current.append(line)
    if current:
        paragraphs.append(current)

    if not paragraphs or paragraphs[0][0] != "COLUMNS":
        raise DbdError("file does not start with a COLUMNS paragraph")

    columns: dict[str, ColumnDecl] = {}
    _parse_columns(paragraphs[0][1:], columns)
    blocks = [_parse_block(lines) for lines in paragraphs[1:]]
    return Definition(columns=columns, blocks=blocks)
