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

namespace wowlib::lang
{
  /** C#/.NET — the welder-csharp rod's identity (user-range slot 0), respelled
      for wowlib's annotation sites (`mark::only`, `mark::exclude`, `weld_as`). */
  inline constexpr welder::lang cs{welder::user_lang<0>};
}
