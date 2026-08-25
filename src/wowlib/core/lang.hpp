#pragma once

#include <welder/vocabulary.hpp>

/** @file
    Binding-language identities welder's core does not name.

    welder's language value space is open: indices 0–15 belong to the languages
    welder ships (`welder::lang::py`, `welder::lang::lua`), 16–31 are the user
    range an out-of-tree rod mints its identity from. The welder-csharp rod
    claims user slot 0 (`WELDER_CSHARP_LANG_SLOT`, default 0); wowlib respells
    that same identity here so annotation sites in core headers never include
    the rod's headers — the rod is only fetched on `WOWLIB_BUILD_CSHARP`
    configures, while these headers must parse in every build.

    @warning The two spellings must agree bit-for-bit: if the rod's slot is
    ever re-pointed (a second out-of-tree rod claiming slot 0), re-point this
    constant with it, or every `cs` mark silently stops resolving. */

namespace wowlib::lang {
  /** C#/.NET — the welder-csharp rod's identity (user-range slot 0), respelled
      for wowlib's annotation sites (`mark::only`, `mark::exclude`, `weld_as`). */
  inline constexpr welder::lang cs{welder::user_lang<0>};
}

/** @def WOWLIB_CS_FAMILY_SURFACE
    The C# rod's family-surface opt-in, spellable in every build.

    `[[=welder::rods::csharp::family_surface]]` on a welded `*Base` opts its
    family into the rod-synthesized version-agnostic surface — but the marker
    TYPE lives in the rod, which only `WOWLIB_BUILD_CSHARP` configures fetch,
    while these headers must parse in every build. So the annotation hides
    behind this macro: on C# configures CMake defines `WOWLIB_CSHARP_ROD`
    tree-wide (every TU of a build must agree on a class's annotation list —
    gcc-16 reads annotations off the DEFINING declaration only), the rod's
    marks header is on every include path, and the macro expands to the mark;
    everywhere else it expands to nothing and the annotation never exists.
    The trailing comma rides inside the macro so the empty expansion leaves a
    well-formed annotation list. */
#if defined(WOWLIB_CSHARP_ROD)
#include <welder/rods/csharp/marks.hpp>
#define WOWLIB_CS_FAMILY_SURFACE =welder::rods::csharp::family_surface,
#else
#define WOWLIB_CS_FAMILY_SURFACE
#endif
