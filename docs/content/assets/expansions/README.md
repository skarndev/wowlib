# Expansion icons (optional)

The generic WMO fields page (`docs/wmo_reference.py`) renders each expansion as a
small badge. If an icon file is present here, the badge shows the icon (plus the
version number for mid-expansion fields); otherwise it falls back to a colored text
pill. Drop-in only — no code change needed.

Expected filenames (any of `.svg`, `.png`, `.webp`; SVG preferred for crispness):

| Key | Expansion |
|---|---|
| `vanilla` | Classic |
| `tbc` | The Burning Crusade |
| `wotlk` | Wrath of the Lich King |
| `cata` | Cataclysm |
| `mop` | Mists of Pandaria |
| `wod` | Warlords of Draenor |
| `legion` | Legion |
| `bfa` | Battle for Azeroth |
| `shadowlands` | Shadowlands |
| `dragonflight` | Dragonflight |
| `tww` | The War Within |

Recommended size ~20–32 px tall (they render at ~1em).

**Licensing:** ship only icons you have the right to redistribute — your own
artwork, or a set under a license that permits redistribution (with attribution as
required). Blizzard's official expansion logos are copyrighted/trademarked and are
**not** included here; hosting on a fan wiki does not grant redistribution rights.
