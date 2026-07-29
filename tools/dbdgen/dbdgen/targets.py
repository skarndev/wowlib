"""The client versions wowlib targets (the last minor of every major) and the
era-dependent column facts the emitter needs."""

from __future__ import annotations

from dataclasses import dataclass

from dbdgen.dbd import Build


@dataclass(frozen=True)
class Target:
    """One targeted client release."""

    era: str  # the wowlib versions:: constant name ("vanilla")
    suffix: str  # the CamelCase spelling used in class names ("Vanilla")
    version: Build


# Mirrors wowlib::versions (src/wowlib/core/client_version.hpp); same order.
TARGETS: tuple[Target, ...] = (
    Target("vanilla", "Vanilla", (1, 12, 1, 5875)),
    Target("tbc", "Tbc", (2, 4, 3, 8606)),
    Target("wotlk", "Wotlk", (3, 3, 5, 12340)),
    Target("cata", "Cata", (4, 3, 4, 15595)),
    Target("mop", "Mop", (5, 4, 8, 18414)),
    Target("wod", "Wod", (6, 2, 4, 21742)),
    Target("legion", "Legion", (7, 3, 5, 26972)),
    Target("bfa", "Bfa", (8, 3, 7, 35662)),
    Target("shadowlands", "Shadowlands", (9, 2, 7, 45745)),
    Target("dragonflight", "Dragonflight", (10, 2, 7, 55664)),
    Target("tww", "Tww", (11, 2, 7, 65299)),
)

TARGETS_BY_ERA: dict[str, Target] = {t.era: t for t in TARGETS}


def locstring_langs(build: Build) -> int | None:
    """The language slot count of a locstring column in ``build``: 8 before
    TBC 2.1.0.6692 added ruRU, 16 until Cataclysm collapsed localized columns
    to a single already-localized string (None)."""
    if build < (2, 1, 0, 6692):
        return 8
    if build < (4, 0, 0, 0):
        return 16
    return None
